#include "kernel.h"

/*
 * Interrupt Descriptor Table (IDT) for x86-64
 * Handles interrupts and exceptions
 */

// IDT entry structure (16 bytes)
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

// IDT pointer structure
typedef struct {
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed)) idt_pointer_t;

// Number of IDT entries
#define IDT_ENTRIES 256

// Interrupt/IRQ numbers
#define DIVIDE_BY_ZERO        0
#define DEBUG_EXCEPTION       1
#define NON_MASKABLE_INT      2
#define BREAKPOINT            3
#define OVERFLOW              4
#define BOUND_RANGE_EXCEEDED  5
#define INVALID_OPCODE        6
#define DEVICE_NOT_AVAIL      7
#define DOUBLE_FAULT          8
#define COPROCESSOR_SEG_OVR   9
#define INVALID_TSS          10
#define SEGMENT_NOT_PRESENT  11
#define STACK_SEGMENT_FAULT  12
#define GENERAL_PROTECTION   13
#define PAGE_FAULT           14
#define RESERVED             15
#define FLOATING_POINT_ERR   16
#define ALIGNMENT_CHECK      17
#define MACHINE_CHECK        18
#define SIMD_FLOATING_POINT  19

// IRQ remapping (master PIC vectors)
#define IRQ0  32  // Timer
#define IRQ1  33  // Keyboard
#define IRQ2  34  // Cascade (PIC)
#define IRQ3  35  // COM2
#define IRQ4  36  // COM1
#define IRQ5  37  // LPT2
#define IRQ6  38  // Floppy
#define IRQ7  39  // LPT1
#define IRQ8  40  // RTC
#define IRQ9  41  // Redirect to IRQ2
#define IRQ10 42  // Reserved
#define IRQ11 43  // Reserved
#define IRQ12 44  // Mouse
#define IRQ13 45  // FPU
#define IRQ14 46  // Primary ATA
#define IRQ15 47  // Secondary ATA

// IDT and pointer
static idt_entry_t idt[IDT_ENTRIES];
static idt_pointer_t idt_ptr;

// External function declarations
extern void interrupt_handler(void);  // C handler for interrupts
extern void* isr_table[];             // Table of ISR entry points

// Type attributes for IDT entries
#define IDT_TYPE_INTERRUPT_GATE 0x8E
#define IDT_TYPE_TRAP_GATE      0x8F

// Forward declarations
static void idt_set_entry(uint8_t num, uintptr_t offset, uint16_t selector,
                         uint8_t type_attr);

// Initialize IDT
void idt_init(void)
{
    KINFO("Initializing IDT...");

    // Set up IDT pointer
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uintptr_t)&idt;

    // Clear IDT
    memset(&idt, 0, sizeof(idt));

    // Set up all ISR entries (0-47) from the assembly ISR table
    for (int i = 0; i < 48; i++) {
        idt_set_entry(i, (uintptr_t)isr_table[i], 0x08, IDT_TYPE_INTERRUPT_GATE);
    }

    // Set up system call ISR (128) - stored at index 48 in the table
    idt_set_entry(128, (uintptr_t)isr_table[48], 0x08, IDT_TYPE_INTERRUPT_GATE);

    // Load IDT
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    KINFO("IDT initialized with %d entries", 49);
}

// Set an IDT entry
static void idt_set_entry(uint8_t num, uintptr_t offset, uint16_t selector,
                         uint8_t type_attr)
{
    idt[num].offset_low = offset & 0xFFFF;
    idt[num].offset_middle = (offset >> 16) & 0xFFFF;
    idt[num].offset_high = (offset >> 32) & 0xFFFFFFFF;

    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
}
