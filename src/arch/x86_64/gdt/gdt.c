#include "kernel.h"

/*
 * Global Descriptor Table (GDT) for x86-64
 * Defines memory segments and privilege levels
 */

// GDT entry structure (8 bytes)
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __PACKED gdt_entry_t;

// Extended GDT entry for x86-64 (16 bytes)
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

// GDT pointer structure
typedef struct {
    uint16_t limit;
    uintptr_t base;
} __PACKED gdt_pointer_t;

// Global GDT and pointer
static gdt_entry_t gdt[5];
static gdt_pointer_t gdt_ptr;

// Access flags for GDT entries
#define GDT_ACCESS_PRESENT     0x80
#define GDT_ACCESS_RING0       0x00
#define GDT_ACCESS_RING3       0x60
#define GDT_ACCESS_SYSTEM      0x00
#define GDT_ACCESS_EXECUTABLE  0x08
#define GDT_ACCESS_CONFORMING  0x04
#define GDT_ACCESS_PRIVILEGE   0x10
#define GDT_ACCESS_DATA_WRITABLE 0x02

// Granularity flags
#define GDT_GRANULARITY_4K     0x80
#define GDT_GRANULARITY_32BIT  0x40

// Segment selectors for our GDT
#define KERNEL_CODE_SEGMENT 0x08
#define KERNEL_DATA_SEGMENT 0x10
#define USER_CODE_SEGMENT   0x18
#define USER_DATA_SEGMENT   0x20
#define TSS_SEGMENT         0x28

// Forward declarations
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t granularity);

// Initialize GDT
void gdt_init(void)
{
    KINFO("Initializing GDT...");

    // Set up GDT pointer
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uintptr_t)&gdt;

    // Null descriptor (required)
    gdt_set_entry(0, 0, 0, 0, 0);

    // Kernel code segment (ring 0, executable)
    gdt_set_entry(1, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // Kernel data segment (ring 0, data)
    gdt_set_entry(2, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User code segment (ring 3, executable)
    gdt_set_entry(3, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User data segment (ring 3, data)
    gdt_set_entry(4, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // Load GDT
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    // Update segment registers
    __asm__ volatile(
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "mov %ax, %ss\n"
        "push $0x08\n"
        "push $.reload_cs\n"
        ".byte 0x48, 0xcb\n"  // retfq opcode
        ".reload_cs:\n"
    );

    KINFO("GDT initialized successfully");
}

// Set a GDT entry
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
