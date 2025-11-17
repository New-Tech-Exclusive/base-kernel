#include "kernel.h"

/*
 * Round-robin task scheduler implementation
 * Handles context switching and CPU time allocation between processes
 */

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct vm_area_struct {
    uintptr_t start;        // Virtual start address
    size_t size;           // Size of mapping
    struct vm_area_struct* next; // Linked list
} vm_area_t;

typedef struct task {
    uint64_t id;               // Unique task identifier
    task_state_t state;        // Current task state
    uint64_t* stack_top;       // Stack pointer (current top)
    uint64_t* stack_bottom;    // Stack base address
    struct task* next;         // Next task in ready queue
    uint64_t ticks_remaining;  // Remaining time slice ticks
    // Memory management for fork() support
    vm_area_t* vm_areas;       // Process address space mappings
} task_t;

// Scheduler state variables
static task_t* current_task = NULL;
static task_t* ready_queue_head = NULL;
static task_t* ready_queue_tail = NULL;
static uint64_t next_task_id = 0;
static uint64_t total_tasks = 0;

// Time slice configuration
#define TASK_TIME_SLICE  10  // Time slice in timer ticks

// Internal scheduler functions
static void scheduler_switch_to_task(task_t* task);
static void scheduler_save_context(task_t* task);
static task_t* scheduler_get_next_task(void);
static void scheduler_schedule(void);
void scheduler_add_to_ready_queue(task_t* task);

/*
 * Initialize the task scheduler
 * Creates idle task and prepares for process management
 */
void scheduler_init(void)
{
    KINFO("Initializing task scheduler...");

    // Allocate idle task structure
    task_t* idle_task = kmalloc(sizeof(task_t));
    if (!idle_task) {
        KERROR("Failed to allocate idle task");
        return;
    }

    // Configure idle task with kernel stack
    idle_task->id = next_task_id++;
    idle_task->state = TASK_RUNNING;
    idle_task->stack_top = (uint64_t*)_kernel_stack_top;
    idle_task->stack_bottom = (uint64_t*)_kernel_stack_bottom;
    idle_task->next = NULL;
    idle_task->ticks_remaining = TASK_TIME_SLICE;

    // Set as currently running task
    current_task = idle_task;
    total_tasks = 1;

    KINFO("Scheduler initialized with %u task(s)", total_tasks);
}

/*
 * Create a new task with specified entry point and stack
 * Returns task ID on success, 0 on failure
 */
uint64_t scheduler_create_task(void (*entry_point)(void), void* stack, size_t stack_size)
{
    // Validate input parameters
    if (!entry_point || !stack || stack_size < PAGE_SIZE) {
        KERROR("Invalid parameters for task creation");
        return 0;
    }

    // Allocate task control block
    task_t* task = kmalloc(sizeof(task_t));
    if (!task) {
        KERROR("Failed to allocate task structure");
        return 0;
    }

    // Initialize task structure
    task->id = next_task_id++;
    task->state = TASK_READY;
    task->stack_bottom = stack;
    task->stack_top = (uint64_t*)((uintptr_t)stack + stack_size);
    task->next = NULL;
    task->ticks_remaining = TASK_TIME_SLICE;

    // Set up initial stack frame to simulate interrupt context
    uint64_t* sp = task->stack_top;

    // Reserve space for register state (simulates what ISR would push)
    sp -= 23;

    // Push return context simulating far call/interrupt return
    *(--sp) = (uint64_t)entry_point;     // RIP - instruction pointer
    *(--sp) = 0x08;                      // CS - code segment selector
    *(--sp) = 0x202;                     // RFLAGS - interrupts enabled
    *(--sp) = (uint64_t)sp + 8;          // RSP - stack pointer
    *(--sp) = 0x10;                      // SS - stack segment selector

    task->stack_top = sp;
    total_tasks++;

    // Add to ready queue for scheduling
    scheduler_add_to_ready_queue(task);

    KINFO("Created task %u (%u total tasks)", task->id, total_tasks);
    return task->id;
}

/*
 * Enqueue a ready task into the scheduler's ready queue
 */
void scheduler_add_to_ready_queue(task_t* task)
{
    if (!task || task->state != TASK_READY)
        return;

    if (!ready_queue_head) {
        ready_queue_head = ready_queue_tail = task;
    } else {
        ready_queue_tail->next = task;
        ready_queue_tail = task;
    }
    task->next = NULL;
}

/*
 * Dequeue the next ready task from the scheduler's ready queue
 */
static task_t* scheduler_remove_from_ready_queue(void)
{
    if (!ready_queue_head)
        return NULL;

    task_t* task = ready_queue_head;
    ready_queue_head = ready_queue_head->next;
    if (!ready_queue_head)
        ready_queue_tail = NULL;

    task->next = NULL;
    return task;
}

/*
 * Timer tick handler for the scheduler
 * Decrements current task's time slice and triggers rescheduling if needed
 */
void scheduler_tick(void)
{
    if (!current_task)
        return;

    // Decrement remaining time slice
    if (current_task->ticks_remaining > 0) {
        current_task->ticks_remaining--;
    }

    // Schedule next task if time slice exhausted
    if (current_task->ticks_remaining == 0) {
        scheduler_schedule();
    }
}

/*
 * Main scheduling algorithm - switch to next ready task
 * Performs round-robin scheduling with time slice preemption
 */
void scheduler_schedule(void)
{
    if (total_tasks <= 1)
        return; // Only idle task present

    task_t* next_task = scheduler_get_next_task();
    if (!next_task || next_task == current_task)
        return; // No viable task switch available

    // Preserve current task state
    scheduler_save_context(current_task);

    // Re-queue current task if still viable
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        current_task->ticks_remaining = TASK_TIME_SLICE;
        scheduler_add_to_ready_queue(current_task);
    }

    // Perform context switch to next task
    scheduler_switch_to_task(next_task);
}

/*
 * Allow current task to voluntarily relinquish CPU
 * Forces immediate rescheduling
 */
void scheduler_yield(void)
{
    if (current_task) {
        current_task->ticks_remaining = 0;
        scheduler_schedule();
    }
}

/*
 * Clean up current task and terminate its execution
 * Reclaims task resources and schedules replacement task
 */
void scheduler_terminate(void)
{
    if (!current_task)
        return;

    KINFO("Terminating task %u", current_task->id);

    current_task->state = TASK_TERMINATED;
    total_tasks--;

    // Schedule replacement task
    scheduler_schedule();

    // Execution should never continue beyond this point
    __asm__ volatile("cli; hlt");
}

/*
 * Retrieve and prepare the next task for execution
 */
static task_t* scheduler_get_next_task(void)
{
    task_t* next = scheduler_remove_from_ready_queue();
    if (next) {
        next->state = TASK_RUNNING;
    }
    return next;
}

/*
 * Save the current task's execution context
 * Stub implementation - full version would save register state
 */
static void scheduler_save_context(task_t* task)
{
    // Complete implementation would save all registers
    // Current version relies on interrupt frame preservation
    (void)task; // Suppress unused parameter warning
}

/*
 * Perform context switch to specified task
 * Updates scheduler state and prepares for task execution
 */
static void scheduler_switch_to_task(task_t* task)
{
    if (!task)
        return;

    current_task = task;

    // Full implementation: restore registers, switch stacks
    // Current version: interrupt return handles context restoration
    KINFO("Switched to task %u", task->id);
}

/*
 * Implement fork() syscall - create a copy of current task
 * Returns child PID to parent, 0 to child, -1 on error
 */
uint64_t scheduler_create_task_fork(void)
{
    if (!current_task) {
        KERROR("No current task to fork from");
        return -1;
    }

    KDEBUG("Forking task %u", current_task->id);

    // Allocate child task structure
    task_t* child_task = kmalloc(sizeof(task_t));
    if (!child_task) {
        KERROR("Failed to allocate child task structure");
        return -1;
    }

    // Copy parent task structure
    memcpy(child_task, current_task, sizeof(task_t));
    child_task->id = next_task_id++;
    child_task->state = TASK_READY;
    child_task->next = NULL;
    child_task->ticks_remaining = TASK_TIME_SLICE;

    // Allocate new stack for child (simplified - in real fork we'd do COW)
    void* child_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!child_stack) {
        KERROR("Failed to allocate child stack");
        kfree(child_task);
        return -1;
    }

    // Copy parent's stack content
    memcpy(child_stack, (void*)current_task->stack_bottom, KERNEL_STACK_SIZE);

    // Set up child's stack pointers
    uintptr_t stack_offset = (uintptr_t)child_stack - (uintptr_t)current_task->stack_bottom;
    child_task->stack_bottom = child_stack;
    child_task->stack_top = (void*)((uintptr_t)current_task->stack_top + stack_offset);

    total_tasks++;

    // Add child to ready queue
    scheduler_add_to_ready_queue(child_task);

    KINFO("Fork successful: parent %u -> child %u (total tasks: %u)",
          current_task->id, child_task->id, total_tasks);

    // Return child PID to parent
    return child_task->id;
}

// Get current task ID
uint64_t scheduler_get_current_task_id(void)
{
    return current_task ? current_task->id : 0;
}

// Get number of tasks
uint64_t scheduler_get_task_count(void)
{
    return total_tasks;
}
