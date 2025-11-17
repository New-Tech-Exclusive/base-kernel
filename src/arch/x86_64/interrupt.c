#include "kernel.h"

/*
 * Interrupt dispatcher
 * Routes interrupts to appropriate handlers
 */

// Interrupt frame structure (matches assembly push order)
typedef struct interrupt_frame {
    // Bottom of stack (pushed first)
    uint64_t error_code;
    uint64_t interrupt_number;

    // Pushed by isr_common_stub
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t ds, es, fs, gs;

    // Pushed by CPU
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

// Exception message table
const char* exception_messages[32] = {
    "Division by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 FPU error",
    "Alignment check",
    "Machine check",
    "SIMD floating point exception",
    "Virtualization exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Forward declarations
static void handle_exception(interrupt_frame_t* frame);
static void handle_irq(interrupt_frame_t* frame);
static void handle_syscall(interrupt_frame_t* frame);

// Main interrupt handler (called by assembly ISR)
void interrupt_handler(interrupt_frame_t* frame)
{
    uint8_t int_num = frame->interrupt_number;

    if (int_num < 32) {
        // CPU exception
        handle_exception(frame);
    } else if (int_num >= 32 && int_num < 48) {
        // IRQ (after PIC remapping)
        handle_irq(frame);
    } else if (int_num == 128) {
        // System call
        handle_syscall(frame);
    } else {
        // Unknown interrupt
        KERROR("Unknown interrupt: %u", int_num);
    }
}

// Handle CPU exceptions
static void handle_exception(interrupt_frame_t* frame)
{
    uint8_t exception = frame->interrupt_number;

    KERROR("CPU Exception %u: %s", exception, exception_messages[exception]);

    // Print additional info for page faults
    if (exception == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        KERROR("Page fault at address 0x%016lx", cr2);

        uint64_t error_code = frame->error_code;
        KERROR("Error code: 0x%016lx", error_code);
        if (error_code & 0x1) KERROR("  Caused by page-level protection violation");
        if (error_code & 0x2) KERROR("  Caused by write access");
        if (error_code & 0x4) KERROR("  Caused by user-mode access");
        if (error_code & 0x8) KERROR("  Caused by reserved bit set");
        if (error_code & 0x10) KERROR("  Caused by instruction fetch");
    } else {
        // Print error code for exceptions that have them
        if (frame->error_code != 0) {
            KERROR("Error code: 0x%016lx", frame->error_code);
        }
    }

    // Print register dump
    KERROR("RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx", frame->rax, frame->rbx, frame->rcx);
    KERROR("RDX=0x%016lx RSI=0x%016lx RDI=0x%016lx", frame->rdx, frame->rsi, frame->rdi);
    KERROR("RBP=0x%016lx RSP=0x%016lx RIP=0x%016lx", frame->rbp, frame->rsp, frame->rip);

    // Halt for exceptions we can't recover from
    if (exception == 8) { // Double fault
        KERROR("Double fault - system halted");
        __asm__ volatile("cli; hlt");
    }

    // For now, just halt. In a real kernel, we'd handle recoverable exceptions
    KERROR("System halted due to unhandled exception");
    __asm__ volatile("cli; hlt");
}

// Handle IRQs (hardware interrupts)
static void handle_irq(interrupt_frame_t* frame)
{
    uint8_t irq_num = frame->interrupt_number - 32;
    uint8_t int_num = frame->interrupt_number;

    // Handle specific IRQs
    switch (irq_num) {
        case 0:  // Timer
            timer_tick();
            break;
        case 1:  // Keyboard
            keyboard_handler();
            break;
        default:
            // Unknown IRQ - just log it
            KWARN("Unhandled IRQ: %u (INT %u)", irq_num, int_num);
            break;
    }

    // Send EOI to PIC
    pic_eoi(irq_num);
}

// Handle system calls
static void handle_syscall(interrupt_frame_t* frame)
{
    uint64_t syscall_num = frame->rax;
    uint64_t arg1 = frame->rdi;
    uint64_t arg2 = frame->rsi;
    uint64_t arg3 = frame->rdx;
    uint64_t arg4 = frame->r10;  // r10, since rcx is overwritten by SYSCALL
    uint64_t arg5 = frame->r8;
    uint64_t arg6 = frame->r9;

    // Dispatch to syscall table
    int64_t retval = syscall_dispatch(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6);

    // Return value goes in RAX
    frame->rax = retval;
}

// Keyboard interrupt handler is implemented in keyboard.c
