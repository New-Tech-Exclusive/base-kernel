#include "kernel.h"

/*
 * Simple round-robin task scheduler
 * Manages task switching and context switching
 */

// Task states
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

// Task structure
typedef struct task {
    uint64_t id;
    task_state_t state;
    uint64_t* stack_top;      // Top of task stack
    uint64_t* stack_bottom;   // Bottom of task stack
    struct task* next;        // Next task in ready queue
    uint64_t ticks_remaining; // Time slice remaining
} task_t;

// Scheduler state
static task_t* current_task = NULL;
static task_t* ready_queue_head = NULL;
static task_t* ready_queue_tail = NULL;
static uint64_t next_task_id = 0;
static uint64_t total_tasks = 0;

// Task time slice (in timer ticks)
#define TASK_TIME_SLICE  10  // 100ms at 100Hz

// Forward declarations
static void scheduler_switch_to_task(task_t* task);
static void scheduler_save_context(task_t* task);
static task_t* scheduler_get_next_task(void);

// Initialize the scheduler
void scheduler_init(void)
{
    KINFO("Initializing task scheduler...");

    // Create idle task (kernel main loop)
    task_t* idle_task = kmalloc(sizeof(task_t));
    if (!idle_task) {
        KERROR("Failed to allocate idle task");
        return;
    }

    idle_task->id = next_task_id++;
    idle_task->state = TASK_RUNNING;
    idle_task->stack_top = (uint64_t*)_kernel_stack_top;
    idle_task->stack_bottom = (uint64_t*)_kernel_stack_bottom;
    idle_task->next = NULL;
    idle_task->ticks_remaining = TASK_TIME_SLICE;

    current_task = idle_task;
    total_tasks = 1;

    KINFO("Scheduler initialized with %u task(s)", total_tasks);
}

// Create a new task
uint64_t scheduler_create_task(void (*entry_point)(void), void* stack, size_t stack_size)
{
    if (!entry_point || !stack || stack_size < PAGE_SIZE) {
        KERROR("Invalid parameters for task creation");
        return 0;
    }

    task_t* task = kmalloc(sizeof(task_t));
    if (!task) {
        KERROR("Failed to allocate task structure");
        return 0;
    }

    // Initialize task
    task->id = next_task_id++;
    task->state = TASK_READY;
    task->stack_bottom = stack;
    task->stack_top = (uint64_t*)((uintptr_t)stack + stack_size);
    task->next = NULL;
    task->ticks_remaining = TASK_TIME_SLICE;

    // Setup initial stack frame (simulate ISR context)
    uint64_t* sp = task->stack_top;

    // Leave space for registers that would be saved by ISR
    sp -= 23; // Skip registers saved by ISR

    // Set up the stack as if an interrupt occurred
    // RIP (instruction pointer)
    *(--sp) = (uint64_t)entry_point;
    // CS, RFLAGS, RSP, SS (simplified - using kernel segments)
    *(--sp) = 0x08; // Kernel code segment
    *(--sp) = 0x202; // RFLAGS (interrupts enabled)
    *(--sp) = (uint64_t)sp + 8; // RSP
    *(--sp) = 0x10; // Kernel data segment

    task->stack_top = sp;
    total_tasks++;

    // Add to ready queue
    scheduler_add_to_ready_queue(task);

    KINFO("Created task %u (%u total tasks)", task->id, total_tasks);
    return task->id;
}

// Add task to ready queue
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

// Remove task from ready queue
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

// Handle timer tick (scheduler tick)
void scheduler_tick(void)
{
    if (!current_task)
        return;

    // Decrement time slice
    if (current_task->ticks_remaining > 0) {
        current_task->ticks_remaining--;
    }

    // Check if time slice expired or task yielded
    if (current_task->ticks_remaining == 0) {
        scheduler_schedule();
    }
}

// Schedule next task (context switch)
void scheduler_schedule(void)
{
    if (total_tasks <= 1)
        return; // Only idle task, no need to switch

    task_t* next_task = scheduler_get_next_task();
    if (!next_task || next_task == current_task)
        return; // No other ready tasks

    // Save current task state
    scheduler_save_context(current_task);

    // Add current task back to ready queue if not terminated
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        current_task->ticks_remaining = TASK_TIME_SLICE;
        scheduler_add_to_ready_queue(current_task);
    }

    // Switch to next task
    scheduler_switch_to_task(next_task);
}

// Yield current task
void scheduler_yield(void)
{
    if (current_task) {
        current_task->ticks_remaining = 0;
        scheduler_schedule();
    }
}

// Terminate current task
void scheduler_terminate(void)
{
    if (!current_task)
        return;

    KINFO("Terminating task %u", current_task->id);

    current_task->state = TASK_TERMINATED;
    total_tasks--;

    // Schedule next task
    scheduler_schedule();

    // Should not reach here
    __asm__ volatile("cli; hlt");
}

// Get the next task to run
static task_t* scheduler_get_next_task(void)
{
    task_t* next = scheduler_remove_from_ready_queue();
    if (next) {
        next->state = TASK_RUNNING;
    }
    return next;
}

// Save current task context (called during context switch)
static void scheduler_save_context(task_t* task)
{
    // In a full implementation, this would save all registers
    // For now, we rely on the interrupt frame being on the stack
    (void)task; // Suppress unused parameter warning
}

// Switch to a new task (context switch)
static void scheduler_switch_to_task(task_t* task)
{
    if (!task)
        return;

    current_task = task;

    // In a full implementation, this would restore registers and switch stacks
    // For now, we assume the interrupt return will handle the actual switch
    // since the task's stack pointer is set up correctly

    KINFO("Switched to task %u", task->id);
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
