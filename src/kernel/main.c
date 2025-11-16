#include "kernel.h"

extern uint32_t multiboot_magic;
extern uint32_t multiboot_info;

/*
 * Base Kernel Main Entry Point
 *
 * This is the main kernel file that initializes all subsystems
 * and starts the first process.
 */

/* Early initialization - called from assembly boot code */
void kernel_early_init(void)
{
    /* Initialize basic kernel infrastructure */
    /* These functions will be implemented as we add features */

    /* Serial output for debugging (very early) */
    serial_init();

    /* VGA text mode */
    vga_init();

    kernel_info("Base Kernel Early Initialization Started");
    kernel_info("Kernel version: 0.1.0 (x86_64)");

    /* Jump to main initialization */
    kernel_init();
}

/* Main kernel initialization */
void kernel_init(void)
{
    kernel_info("Base Kernel Main Initialization");

    /* Initialize physical memory manager (needs identity mapping from bootloader) */
    pmm_init();

    /* Initialize kernel heap (uses PMM) */
    kheap_init();

    /* Set up CPU state - GDT should be after basic memory allocators */
    gdt_init();          /* Global Descriptor Table */

    /* Set up interrupts - IDT MUST be initialized before any interrupts */
    idt_init();          /* Interrupt Descriptor Table */

    /* Set up programmable interrupt controller */
    pic_init();

    /* Initialize virtual memory (extend identity mapping, don't re-enable paging) */
    paging_init();

    /* Initialize devices */
    timer_init();
    keyboard_init();

    /* Scheduler setup (basic framework) */
    scheduler_init();

    /* Initialize VFS */
    vfs_init();

    kernel_info("Kernel initialization complete, enabling interrupts");

    /* Enable interrupts now that everything is set up */
    __asm__ volatile("sti");

    /* Start the main kernel loop */
    kernel_main();
}

/* Main kernel loop - never returns */
void kernel_main(void)
{
    kernel_info("Base Kernel started successfully");

    /* For now, just loop forever */
    /* In a full kernel, this would start init process */
    while (1) {
        /* Check for work to do */
        scheduler_tick();

        /* Handle timers */
        timer_tick();

        /* Yield CPU if nothing to do - in real kernel, this would hlt */
        /* For now, just continue the loop */
    }
}
