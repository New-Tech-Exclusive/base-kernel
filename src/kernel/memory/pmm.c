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

// Enhanced PMM with page frame caching and optimized allocation
static uint8_t* memory_bitmap;
static size_t memory_bitmap_size;
static uintptr_t memory_start;
static size_t total_memory_pages;
static size_t used_memory_pages;

// Page frame cache for common allocation sizes (per CPU)
// Each cache entry holds a list of free pages
#define PF_CACHE_SIZE 32
static uint64_t* pf_cache_hot[PF_CACHE_SIZE];  // Hot cache - recently freed pages
static uint64_t* pf_cache_cold[PF_CACHE_SIZE]; // Cold cache - for page reclamation
static size_t pf_cache_hot_count[PF_CACHE_SIZE];
static size_t pf_cache_cold_count[PF_CACHE_SIZE];

// Allocation statistics for optimization
static size_t alloc_requests = 0;
static size_t alloc_failures = 0;
static size_t total_pages_allocated = 0;
static size_t total_pages_freed = 0;

// External symbols defined by linker script
extern char _kernel_end[];

// Kernel start address loaded by bootloader
#define KERNEL_START_ADDR 0x100000ULL

// Physical memory limits and thresholds
#define MEMORY_MAP_TYPE_AVAILABLE 1
#define MEMORY_MAP_TYPE_RESERVED  2
#define LOW_MEMORY_THRESHOLD      (128 * 1024 * 1024 / PAGE_SIZE)  // 128MB low memory
#define PAGE_CACHE_SIZE          8    // Maximum pages per cache entry
#define BUDDY_MAX_ORDER          11   // 2^11 = 2048KB buddy allocation max

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

// Initialize page frame caches for performance optimization
static void pmm_init_caches(void)
{
    // Allocate memory for page frame caches
    for (int i = 0; i < PAGE_CACHE_SIZE; i++) {
        // Allocate hot cache arrays - small fixed size for now
        if (i == 0) {  // Only initialize single-page cache for simplicity
            pf_cache_hot[0] = kmalloc(PAGE_CACHE_SIZE * sizeof(uint64_t));
            pf_cache_cold[0] = kmalloc(PAGE_CACHE_SIZE * sizeof(uint64_t));

            if (pf_cache_hot[0]) {
                memset(pf_cache_hot[0], 0, PAGE_CACHE_SIZE * sizeof(uint64_t));
            }
            if (pf_cache_cold[0]) {
                memset(pf_cache_cold[0], 0, PAGE_CACHE_SIZE * sizeof(uint64_t));
            }
        }

        pf_cache_hot_count[i] = 0;
        pf_cache_cold_count[i] = 0;
    }
    KINFO("Page frame caches initialized");
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

            // Initialize page frame caches after bitmap setup
            pmm_init_caches();

            return;
        }

        // Next tag
        uintptr_t next_tag = (uintptr_t)tag + ALIGN_UP(tag->size, 8);
        if (next_tag >= mb_info_addr + 1024) break;
        tag = (multiboot_tag_t*)next_tag;
    }

    PANIC("No memory map found in multiboot information");
}

/*
 * Enhanced page allocation with caching and optimization
 * Implements Linux-style page frame caching for better performance
 */
uintptr_t pmm_alloc_pages(size_t num_pages)
{
    alloc_requests++;

    // Input validation
    if (num_pages == 0) return 0;
    if (num_pages > 1024) {  // Reasonable upper limit
        KERROR("PMM: Allocation request too large (%lu pages)", num_pages);
        alloc_failures++;
        return 0;
    }

    // Check memory pressure - if low on memory, avoid caching
    size_t free_pages = pmm_get_free_pages();
    bool low_memory = free_pages < LOW_MEMORY_THRESHOLD;

    // Try page frame cache first for single page allocations
    if (num_pages == 1 && !low_memory && pf_cache_hot_count[0] > 0) {
        // Use cached page if available
        uint64_t page_addr = pf_cache_hot[0][--pf_cache_hot_count[0]];
        pf_cache_hot[0][pf_cache_hot_count[0]] = 0;  // Clear used entry

        // Mark page as used in bitmap
        size_t page_idx = page_addr / PAGE_SIZE;
        memory_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
        used_memory_pages++;
        total_pages_allocated++;

        KDEBUG("PMM: Allocated %lu page(s) from cache (hot): 0x%lx", num_pages, page_addr);
        return page_addr;
    }

    // Standard bitmap-based allocation using first-fit strategy
    size_t consecutive_free = 0;
    size_t start_page = 0;
    size_t best_start = 0;
    size_t best_size = (size_t)-1;  // Best size is smallest block that fits

    // Find the best-fit block
    for (size_t i = 0; i < total_memory_pages; i++) {
        if (!(memory_bitmap[i / 8] & (1 << (i % 8)))) {
            // Page is free
            consecutive_free++;
            if (consecutive_free == 1) {
                start_page = i;
            }
            if (consecutive_free == num_pages) {
                // Found adequate block - check if better than current best
                if (best_size > consecutive_free || best_start == 0) {
                    best_start = start_page;
                    best_size = consecutive_free;
                }
                // Continue searching for better fits
                consecutive_free = 0;  // Reset for next search
            }
        } else {
            consecutive_free = 0;
        }
    }

    if (best_start == 0) {
        KERROR("PMM: Out of memory, requested %lu pages", num_pages);
        alloc_failures++;
        return 0; // Out of memory
    }

    // Mark pages as used in bitmap
    for (size_t j = 0; j < num_pages; j++) {
        size_t page = best_start + j;
        memory_bitmap[page / 8] |= (1 << (page % 8));
    }

    used_memory_pages += num_pages;
    total_pages_allocated += num_pages;

    uintptr_t allocated_addr = best_start * PAGE_SIZE;

    // Add neighboring free pages to hot cache (buddy-style)
    if (!low_memory && num_pages == 1) {
        // Look for highest-indexed free page after allocation for LIFO behavior
        for (size_t i = total_memory_pages - 1; i > best_start + num_pages; i--) {
            if (!(memory_bitmap[i / 8] & (1 << (i % 8)))) {
                // Cache in hot list if room
                if (pf_cache_hot_count[0] < PAGE_CACHE_SIZE) {
                    pf_cache_hot[0][pf_cache_hot_count[0]++] = i * PAGE_SIZE;
                }
                break;  // Just cache one for now
            }
        }
    }

    KDEBUG("PMM: Allocated %lu page(s) from bitmap: 0x%lx", num_pages, allocated_addr);
    return allocated_addr;
}

/*
 * Enhanced page deallocation with intelligent caching
 * Implements page frame caching similar to Linux
 */
void pmm_free_pages(uintptr_t addr, size_t num_pages)
{
    if (num_pages == 0 || addr == 0) return;

    // Input validation
    size_t start_page = addr / PAGE_SIZE;
    if (start_page >= total_memory_pages) {
        KWARN("PMM: Free request for invalid address: 0x%lx", addr);
        return;
    }

    // Check if pages are actually allocated
    bool all_allocated = true;
    for (size_t i = 0; i < num_pages && all_allocated; i++) {
        size_t page = start_page + i;
        if (!(memory_bitmap[page / 8] & (1 << (page % 8)))) {
            all_allocated = false;
        }
    }

    if (!all_allocated) {
        KWARN("PMM: Double-free detected at 0x%lx - pages not marked as allocated", addr);
        return;
    }

    // Single page allocation - consider caching
    if (num_pages == 1) {
        // Check memory pressure for caching decisions
        size_t free_pages = pmm_get_free_pages();
        bool should_cache = free_pages > (LOW_MEMORY_THRESHOLD * 2);

        if (should_cache && pf_cache_hot_count[0] < PAGE_CACHE_SIZE) {
            // Add to hot cache for quick reuse (LIFO)
            pf_cache_hot[0][pf_cache_hot_count[0]++] = addr;
            total_pages_freed++;
            KDEBUG("PMM: Cached %lu page(s) in hot cache: 0x%lx", num_pages, addr);
            return;
        }
    }

    // Standard bitmap deallocation
    for (size_t i = 0; i < num_pages; i++) {
        size_t page = start_page + i;
        memory_bitmap[page / 8] &= ~(1 << (page % 8));
    }

    used_memory_pages -= num_pages;
    total_pages_freed += num_pages;

    KDEBUG("PMM: Freed %lu page(s) at 0x%lx", num_pages, addr);
}

/*
 * Get detailed PMM statistics for monitoring and optimization
 */
void pmm_get_stats(size_t* requests, size_t* failures, size_t* cache_hit_rate,
                  size_t* fragmentation_ratio)
{
    *requests = alloc_requests;
    *failures = alloc_failures;

    // Calculate cache hit ratio (simplified)
    size_t total_single_allocs = 0;  // Would need per-size tracking
    *cache_hit_rate = total_single_allocs > 0 ?
                     (pf_cache_hot_count[0] * 100) / total_single_allocs : 0;

    // Simple fragmentation calculation - ratio of free pages to largest free block
    size_t max_consecutive_free = 0;
    size_t current_free = 0;
    for (size_t i = 0; i < total_memory_pages; i++) {
        if (!(memory_bitmap[i / 8] & (1 << (i % 8)))) {
            current_free++;
            if (current_free > max_consecutive_free) {
                max_consecutive_free = current_free;
            }
        } else {
            current_free = 0;
        }
    }

    size_t total_free = pmm_get_free_pages();
    *fragmentation_ratio = total_free > 0 ?
                          ((total_free - max_consecutive_free) * 100) / total_free : 0;
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
