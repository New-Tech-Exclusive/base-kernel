#include "kernel.h"

/*
 * Global Descriptor Table (GDT) management for x86-64
 * Provides segment-based memory protection and system organization
 */

// Standard GDT entry - 8 bytes for legacy compatibility
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __PACKED gdt_entry_t;

// Extended GDT entry for 64-bit addressing - 16 bytes total
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __PACKED gdt_extended_entry_t;

// GDTR register structure for LGDT instruction
typedef struct {
    uint16_t limit;
    uintptr_t base;
} __PACKED gdt_pointer_t;

// Global GDT table and descriptor
static gdt_entry_t gdt[5];
static gdt_pointer_t gdt_ptr;

// Access byte bitfield definitions
#define GDT_ACCESS_PRESENT        0x80  // Segment is present in memory
#define GDT_ACCESS_RING0          0x00  // Privilege level 0 (kernel)
#define GDT_ACCESS_RING3          0x60  // Privilege level 3 (user)
#define GDT_ACCESS_SYSTEM         0x00  // System segment (not code/data)
#define GDT_ACCESS_EXECUTABLE     0x08  // Code segment (executable)
#define GDT_ACCESS_CONFORMING     0x04  // Privilege level conforming bit
#define GDT_ACCESS_PRIVILEGE      0x10  // Read access for code, expand-down for data
#define GDT_ACCESS_DATA_WRITABLE  0x02  // Write access for data segments

// Granularity byte settings
#define GDT_GRANULARITY_4K        0x80  // 4KB granularity for limit
#define GDT_GRANULARITY_32BIT     0x40  // 32-bit operand size default

// Common segment selectors used throughout the kernel
#define KERNEL_CODE_SEGMENT 0x08  // Kernel code segment selector
#define KERNEL_DATA_SEGMENT 0x10  // Kernel data segment selector
#define USER_CODE_SEGMENT   0x18  // User code segment selector
#define USER_DATA_SEGMENT   0x20  // User data segment selector
#define TSS_SEGMENT         0x28  // Task State Segment selector

/*
 * Forward declaration for GDT entry setup function
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t granularity);

/*
 * Initialize the Global Descriptor Table
 * Sets up segment descriptors for kernel and user mode
 */
void gdt_init(void)
{
    KINFO("Initializing GDT...");

    // Initialize GDTR with table address and size
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uintptr_t)&gdt;

    // Null descriptor - required by x86 architecture
    gdt_set_entry(0, 0, 0, 0, 0);

    // Kernel code segment: ring 0, executable, readable
    gdt_set_entry(1, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // Kernel data segment: ring 0, writable, accessible
    gdt_set_entry(2, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User code segment: ring 3, executable, readable
    gdt_set_entry(3, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User data segment: ring 3, writable, accessible
    gdt_set_entry(4, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // Load GDT register with our table
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    // Reload segment registers with new selectors
    __asm__ volatile(
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "mov %ax, %ss\n"
        "push $0x08\n"
        "push $.reload_cs\n"
        ".byte 0x48, 0xcb\n"  // RETFQ: return from far, quadword (64-bit)
        ".reload_cs:\n"
    );

    KINFO("GDT initialized successfully");
}

/*
 * Configure a single GDT entry with the provided parameters
 * Splits base/limit values across descriptor fields per x86 spec
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t granularity)
{
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;

    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = (limit >> 16) & 0x0F;

    gdt[index].granularity |= granularity & 0xF0;
    gdt[index].access = access;
}
