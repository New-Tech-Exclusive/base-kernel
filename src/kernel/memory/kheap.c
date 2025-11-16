#include "kernel.h"

// Simple bump allocator for early kernel boot
// Uses identity mapped memory below the kernel area
// The bootloader maps the first few MB identity, so we can use them directly

#define EARLY_HEAP_START  0x300000  // Start at 3MB mark (safe after kernel)
#define EARLY_HEAP_SIZE   0x100000  // 1MB early heap
#define EARLY_HEAP_END    (EARLY_HEAP_START + EARLY_HEAP_SIZE)

// Bump allocator state - start high and allocate downward
static uintptr_t heap_next_free = EARLY_HEAP_START;

// Initialize the kernel heap
void kheap_init(void)
{
    KINFO("Initializing kernel heap...");

    // Clear early heap area to ensure clean memory
    memset((void*)EARLY_HEAP_START, 0, EARLY_HEAP_SIZE);

    KINFO("Kernel heap initialized (%u KB available)", EARLY_HEAP_SIZE / 1024);
}

// Allocate memory from kernel heap (simple bump allocation for now)
void* kmalloc(size_t size)
{
    if (size == 0) return NULL;

    // Align size to 16 bytes for safety
    size = (size + 15) & ~15ULL;

    if (heap_next_free + size > EARLY_HEAP_END) {
        KERROR("Early heap exhausted - kmalloc failed (requested %zu bytes)", size);
        return NULL;
    }

    void* ptr = (void*)heap_next_free;
    heap_next_free += size;

    return ptr;
}

// Free memory (no-op for bump allocator - allocations are permanent)
void kfree(void* ptr)
{
    // For early boot, we don't free memory - it's a bump allocator
    // In a full kernel, this would be a real heap manager
    (void)ptr; // Suppress unused parameter warning
}
