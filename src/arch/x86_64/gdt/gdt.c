#include "kernel.h"

/*
 * Our friendly roadmap for the CPU!
 * This table explains what different parts of memory are for and who can access them
 */

// A mini package describing each memory zone (8 bytes worth of details)
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t base_high;
    uint8_t access;
    uint8_t granularity;
} __PACKED gdt_entry_t;

// Bigger package for fancy 64-bit memory info (16 cozy bytes)
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

// This tells the CPU exactly where our roadmap lives in memory
typedef struct {
    uint16_t limit;
    uintptr_t base;
} __PACKED gdt_pointer_t;

// Our main roadmap and the sign pointing to it
static gdt_entry_t gdt[5];
static gdt_pointer_t gdt_ptr;

// Permission flags that decide who gets to use each memory zone
#define GDT_ACCESS_PRESENT     0x80  // "I'm here!" flag
#define GDT_ACCESS_RING0       0x00  // Super admin level
#define GDT_ACCESS_RING3       0x60  // Regular user level
#define GDT_ACCESS_SYSTEM      0x00  // System code/data
#define GDT_ACCESS_EXECUTABLE  0x08  // Can run code here
#define GDT_ACCESS_CONFORMING  0x04  // Special behavior flag
#define GDT_ACCESS_PRIVILEGE   0x10  // Read access allowed
#define GDT_ACCESS_DATA_WRITABLE 0x02  // Can write data here

// Size settings for memory zones
#define GDT_GRANULARITY_4K     0x80  // Use 4KB page sizes
#define GDT_GRANULARITY_32BIT  0x40  // 32-bit friendly

// Shortcuts to find zones in our roadmap
#define KERNEL_CODE_SEGMENT 0x08  // Where the system code lives
#define KERNEL_DATA_SEGMENT 0x10  // Where system data hangs out
#define USER_CODE_SEGMENT   0x18  // User's coding playground
#define USER_DATA_SEGMENT   0x20  // User's data storage
#define TSS_SEGMENT         0x28  // Task State Segment (advanced topic!)

// Telling the compiler we'll define this function later
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t granularity);

// Set up our memory roadmap for the CPU
void gdt_init(void)
{
    KINFO("Setting up the CPU's memory roadmap...");

    // Tell CPU where our roadmap is and how big it is
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uintptr_t)&gdt;

    // First entry must be empty (CPU tradition!)
    gdt_set_entry(0, 0, 0, 0, 0);

    // System code zone - runs with full admin powers
    gdt_set_entry(1, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // System data zone - storage for admin-level data
    gdt_set_entry(2, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User code zone - runs with regular user permissions
    gdt_set_entry(3, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_PRIVILEGE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // User data zone - storage for regular user data
    gdt_set_entry(4, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM |
                  GDT_ACCESS_PRIVILEGE | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    // Give the roadmap to the CPU
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    // Update CPU's segment registers to use our new setup
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
        ".byte 0x48, 0xcb\n"  // Special "update code segment" instruction
        ".reload_cs:\n"
    );

    KINFO("CPU roadmap is ready to go!");
}

// Fill in one entry in our memory roadmap
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
