#include "kernel.h"

// Stub scheduler implementation for basic kernel boot
void scheduler_init(void)
{
    // Minimal stub implementation
    KINFO("Scheduler stub initialized");
}

void scheduler_tick(void)
{
    // Stub implementation
}

void scheduler_yield(void)
{
    // Stub implementation
}

void scheduler_terminate(void)
{
    // Stub implementation
    __asm__ volatile("cli; hlt");
}

uint64_t scheduler_get_current_task_id(void)
{
    return 0;
}

uint64_t scheduler_get_task_count(void)
{
    return 1;
}
