#include "kernel.h"

/*
 * Physical Memory Manager (PMM)
 * Manages allocation of physical memory pages
 */

// Multiboot 2 information structures
typedef struct {
    uint32_t type;
    uint32_t size;
} __PACKED multiboot_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __PACKED multiboot_memory_map_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __PACKED multiboot_memory_map_entry_t;

// Memory bitmap
static uint8_t* memory_bitmap;
static size_t memory_bitmap_size;
static uintptr_t memory_start;
static size_t total_memory_pages;
static size_t used_memory_pages;

// External symbols defined by linker script
extern char _kernel_end[];

// Kernel start address loaded by bootloader
#define KERNEL_START_ADDR 0x100000ULL

// Physical memory limits
#define MEMORY_MAP_TYPE_AVAILABLE 1
#define MEMORY_MAP_TYPE_RESERVED  2

// Forward declarations
static void pmm_parse_memory_map(void);

// Initialize the physical memory manager
void pmm_init(void)
{
    KINFO("Initializing physical memory manager...");

    // Parse multiboot memory map
    pmm_parse_memory_map();

    KINFO("PMM initialized: %llu MB total, %llu MB available",
          (total_memory_pages * PAGE_SIZE) / (1024 * 1024),
          ((total_memory_pages - used_memory_pages) * PAGE_SIZE) / (1024 * 1024));
}

// Parse multiboot memory map and initialize bitmap
static void pmm_parse_memory_map(void)
{
    // Get multiboot info pointer (32-bit initially)
    uintptr_t mb_info_addr = (uintptr_t)(uintptr_t)multiboot_info;

    if (mb_info_addr == 0) {
        PANIC("No multiboot information available");
    }

    multiboot_tag_t* tag = (multiboot_tag_t*)(mb_info_addr + 8); // Skip total_size and reserved

    // Find memory map tag (type 6)
    while ((uintptr_t)tag < mb_info_addr + 1024) { // Reasonable limit
        if (tag->type == 6) { // Memory map tag
            multiboot_memory_map_t* mmap_tag = (multiboot_memory_map_t*)tag;
            multiboot_memory_map_entry_t* entry = (multiboot_memory_map_entry_t*)(mmap_tag + 1);

            size_t num_entries = (mmap_tag->size - sizeof(multiboot_memory_map_t)) /
                                mmap_tag->entry_size;

            // Find the largest available memory region for bitmap
            uintptr_t bitmap_addr = 0;
            size_t largest_region_size = 0;

            for (size_t i = 0; i < num_entries; i++, entry++) {
                uint64_t base = entry->base_addr;
                uint64_t len = entry->length;
                uint32_t type = entry->type;

                if (type == MEMORY_MAP_TYPE_AVAILABLE && len > largest_region_size) {
                    // Available memory, suitable for bitmap
                    largest_region_size = len;
                    bitmap_addr = base;
                }
            }

            // Use part of the largest region for bitmap
            if (bitmap_addr == 0) {
                PANIC("No suitable memory region for bitmap");
            }

            // Calculate memory bitmap size (1 bit per page)
            memory_start = bitmap_addr;
            total_memory_pages = (uint32_t)(PHYSICAL_MEMORY_LIMIT / PAGE_SIZE);
            memory_bitmap_size = ALIGN_UP(total_memory_pages / 8, PAGE_SIZE);

            // Place bitmap at the end of the kernel or start of this region
            memory_bitmap = (uint8_t*)ALIGN_UP((uintptr_t)&_kernel_end, PAGE_SIZE);
            memset(memory_bitmap, 0, memory_bitmap_size);

            // Mark memory as used up to memory_bitmap + size
            uintptr_t used_end = (uintptr_t)memory_bitmap + memory_bitmap_size;
            size_t used_pages = (used_end + PAGE_SIZE - 1) / PAGE_SIZE;
            used_memory_pages = used_pages;

            // Mark kernel memory as used (from kernel start 0x100000 to end)
            uintptr_t kernel_start_page = KERNEL_START_ADDR / PAGE_SIZE;
            uintptr_t kernel_end_page = ALIGN_UP((uintptr_t)&_kernel_end, PAGE_SIZE) / PAGE_SIZE;

            for (size_t i = kernel_start_page; i < kernel_end_page; i++) {
                memory_bitmap[i / 8] |= (1 << (i % 8));
                used_memory_pages++;
            }

            // Mark multiboot info as used
            uintptr_t mb_start = mb_info_addr / PAGE_SIZE;
            uintptr_t mb_end = ALIGN_UP(mb_info_addr + 1024, PAGE_SIZE) / PAGE_SIZE;

            for (size_t i = mb_start; i < mb_end; i++) {
                memory_bitmap[i / 8] |= (1 << (i % 8));
                used_memory_pages++;
            }

            KINFO("Memory bitmap at 0x%lx, size %lu KB", memory_bitmap, memory_bitmap_size / 1024);
            return;
        }

        // Next tag
        uintptr_t next_tag = (uintptr_t)tag + ALIGN_UP(tag->size, 8);
        if (next_tag >= mb_info_addr + 1024) break;
        tag = (multiboot_tag_t*)next_tag;
    }

    PANIC("No memory map found in multiboot information");
}

// Allocate physical pages
uintptr_t pmm_alloc_pages(size_t num_pages)
{
    if (num_pages == 0) return 0;

    size_t consecutive_free = 0;
    size_t start_page = 0;

    for (size_t i = 0; i < total_memory_pages; i++) {
        if (!(memory_bitmap[i / 8] & (1 << (i % 8)))) {
            // Page is free
            consecutive_free++;
            if (consecutive_free == 1) {
                start_page = i;
            }
            if (consecutive_free == num_pages) {
                // Found enough consecutive pages, mark them as used
                for (size_t j = 0; j < num_pages; j++) {
                    memory_bitmap[(start_page + j) / 8] |= (1 << ((start_page + j) % 8));
                }
                used_memory_pages += num_pages;
                return start_page * PAGE_SIZE;
            }
        } else {
            consecutive_free = 0;
        }
    }

    KERROR("PMM: Out of memory, requested %lu pages", num_pages);
    return 0; // Out of memory
}

// Free physical pages
void pmm_free_pages(uintptr_t addr, size_t num_pages)
{
    if (num_pages == 0) return;

    size_t start_page = addr / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        size_t page = start_page + i;
        memory_bitmap[page / 8] &= ~(1 << (page % 8));
    }

    used_memory_pages -= num_pages;
}

// Get total memory in pages
size_t pmm_get_total_pages(void)
{
    return total_memory_pages;
}

// Get free memory in pages
size_t pmm_get_free_pages(void)
{
    return total_memory_pages - used_memory_pages;
}
