#include "kernel.h"

/*
 * Our friendly task manager!
 * This rotates between different running programs fairly
 * and helps switch between them smoothly
 */

// What mood each task is in right now
typedef enum {
    TASK_READY,      // "Ready to party!"
    TASK_RUNNING,    // "I'm doing my thing!"
    TASK_BLOCKED,    // "Waiting for something"
    TASK_TERMINATED  // "All done, see ya!"
} task_state_t;

// All about a single task - like a player in our game
typedef struct task {
    uint64_t id;               // Like a name tag for each task
    task_state_t state;        // What's this task up to?
    uint64_t* stack_top;       // Top of stack (high address)
    uint64_t* stack_bottom;    // Bottom of stack (low address)
    struct task* next;         // Next player in line
    uint64_t ticks_remaining;  // How much time left in my turn?
} task_t;

// Our task management team - keeping track of who is doing what
static task_t* current_task = NULL;          // Who's running right now?
static task_t* ready_queue_head = NULL;      // First person waiting for their turn
static task_t* ready_queue_tail = NULL;      // Last person waiting for their turn
static uint64_t next_task_id = 0;            // Next available ID number
static uint64_t total_tasks = 0;             // How many players are in the game?

// How long each task gets to have their fun (measured in timer beats)
#define TASK_TIME_SLICE  10  // ~100ms at 100Hz - enough for a quick dance

// Telling the compiler we'll define these helper functions later
static void scheduler_switch_to_task(task_t* task);
static void scheduler_save_context(task_t* task);
static task_t* scheduler_get_next_task(void);
static void scheduler_schedule(void);
void scheduler_add_to_ready_queue(task_t* task);

// Get the party started - set up our task manager
void scheduler_init(void)
{
    KINFO("Waking up our task organizer...");

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

// Let's create a new task - like inviting a new player to our party!
uint64_t scheduler_create_task(void (*entry_point)(void), void* stack, size_t stack_size)
{
    if (!entry_point || !stack || stack_size < PAGE_SIZE) {
        KERROR("Hmm, the new player needs valid information!");
        return 0;
    }

    task_t* task = kmalloc(sizeof(task_t));
    if (!task) {
        KERROR("Oops, couldn't set up the player's information!");
        return 0;
    }

    // Give our new player an ID and get them ready to play
    task->id = next_task_id++;
    task->state = TASK_READY;
    task->stack_bottom = stack;
    task->stack_top = (uint64_t*)((uintptr_t)stack + stack_size);
    task->next = NULL;
    task->ticks_remaining = TASK_TIME_SLICE;

    // Set up their playing area (simulate interrupt environment)
    uint64_t* sp = task->stack_top;

    // Make room for saved registers (what computer needs to remember)
    sp -= 23; // Skip registers that interrupt would save

    // Prepare the stack as if an interrupt just happened
    // Their "next instruction" to run when they start
    *(--sp) = (uint64_t)entry_point;
    // System settings: code segment, interrupts enabled, etc.
    *(--sp) = 0x08; // Kernel code segment selector
    *(--sp) = 0x202; // Flags (interrupts on, important!)
    *(--sp) = (uint64_t)sp + 8; // Where their stack will be
    *(--sp) = 0x10; // Kernel data segment selector

    task->stack_top = sp;
    total_tasks++;

    // Add them to the waiting line
    scheduler_add_to_ready_queue(task);

    KINFO("New player %u joined the game (%u total now)!", task->id, total_tasks);
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
