/*
 * Enhanced Memory Management API Implementation
 * Provides developer-friendly memory allocation and management
 */

#include "kernel.h"

// ============================================================================
// MEMORY TRACKING
// ============================================================================

typedef struct allocation {
    void* ptr;
    size_t size;
    const char* tag;
    uint64_t timestamp;
    struct allocation* next;
} allocation_t;

static allocation_t* allocation_list = NULL;
static memory_stats_t global_stats = {0};
static bool tracking_enabled = true;

// Tracked memory allocation
void* kmalloc_tracked(size_t size, const char* tag) {
    if (!tracking_enabled) {
        return kmalloc(size);
    }

    void* ptr = kmalloc(size);
    if (!ptr) return NULL;

    // Add to tracking list
    allocation_t* alloc = kmalloc(sizeof(allocation_t));
    if (alloc) {
        alloc->ptr = ptr;
        alloc->size = size;
        alloc->tag = tag;
        alloc->timestamp = time_monotonic_ms();
        alloc->next = allocation_list;
        allocation_list = alloc;
    }

    // Update stats
    global_stats.total_allocated += size;
    global_stats.allocations++;
    if (global_stats.total_allocated > global_stats.peak_usage) {
        global_stats.peak_usage = global_stats.total_allocated;
    }

    return ptr;
}

void* krealloc_tracked(void* ptr, size_t size, const char* tag) {
    if (!tracking_enabled) {
        return krealloc(ptr, size);
    }

    // Find existing allocation
    allocation_t** current = &allocation_list;
    while (*current) {
        if ((*current)->ptr == ptr) {
            global_stats.total_allocated -= (*current)->size;
            break;
        }
        current = &(*current)->next;
    }

    void* new_ptr = krealloc(ptr, size);
    if (!new_ptr) {
        // Restore old size if realloc failed
        if (*current) {
            global_stats.total_allocated += (*current)->size;
        }
        return NULL;
    }

    // Update tracking
    if (*current) {
        (*current)->ptr = new_ptr;
        (*current)->size = size;
        (*current)->tag = tag;
    }

    global_stats.total_allocated += size;
    if (global_stats.total_allocated > global_stats.peak_usage) {
        global_stats.peak_usage = global_stats.total_allocated;
    }

    return new_ptr;
}

void kfree_tracked(void* ptr) {
    if (!ptr) return;

    if (!tracking_enabled) {
        kfree(ptr);
        return;
    }

    // Remove from tracking list and update stats
    allocation_t** current = &allocation_list;
    while (*current) {
        if ((*current)->ptr == ptr) {
            global_stats.total_allocated -= (*current)->size;
            global_stats.deallocations++;

            allocation_t* to_free = *current;
            *current = (*current)->next;
            kfree(to_free);
            break;
        }
        current = &(*current)->next;
    }

    // Always free the actual memory
    kfree(ptr);
}

void memory_get_stats(memory_stats_t* stats) {
    if (stats) {
        *stats = global_stats;
    }
}

void memory_dump_leaks(void) {
    if (!tracking_enabled) {
        KINFO("Memory leak detection disabled");
        return;
    }

    KINFO("=== MEMORY LEAK REPORT ===");
    allocation_t* current = allocation_list;
    size_t leak_count = 0;
    size_t leak_size = 0;

    while (current) {
        KINFO("LEAK: %zu bytes at %p (tag: %s, allocated at %llu ms)",
              current->size, current->ptr,
              current->tag ? current->tag : "unknown",
              current->timestamp);
        leak_count++;
        leak_size += current->size;
        current = current->next;
    }

    KINFO("Total leaks: %zu allocations (%zu bytes)", leak_count, leak_size);
    KINFO("=========================");
}

// ============================================================================
// MEMORY POOLS
// ============================================================================

typedef struct memory_block {
    struct memory_block* next;
    char data[];  // Flexible array member
} memory_block_t;

struct memory_pool {
    size_t block_size;
    memory_block_t* free_list;
    size_t allocated_blocks;
    char* pool_start;    // For cleanup
    size_t pool_size;
};

memory_pool_t* memory_pool_create(size_t block_size, size_t initial_blocks) {
    // Adjust block size to accommodate metadata
    block_size = (block_size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);

    size_t total_size = initial_blocks * (block_size + sizeof(memory_block_t));
    char* pool_memory = (char*)kmalloc_tracked(total_size, "memory_pool");

    if (!pool_memory) {
        return NULL;
    }

    memory_pool_t* pool = (memory_pool_t*)kmalloc_tracked(sizeof(memory_pool_t), "memory_pool_struct");
    if (!pool) {
        kfree_tracked(pool_memory);
        return NULL;
    }

    pool->block_size = block_size;
    pool->free_list = NULL;
    pool->allocated_blocks = initial_blocks;
    pool->pool_start = pool_memory;
    pool->pool_size = total_size;

    // Initialize free list
    memory_block_t* block = (memory_block_t*)pool_memory;
    for (size_t i = 0; i < initial_blocks; i++) {
        block->next = pool->free_list;
        pool->free_list = block;
        char* next_addr = (char*)block + sizeof(memory_block_t) + block_size;
        block = (memory_block_t*)next_addr;
    }

    return pool;
}

void* memory_pool_alloc(memory_pool_t* pool) {
    if (!pool->free_list) {
        KERROR("Memory pool exhausted, no free blocks");
        return NULL;
    }

    memory_block_t* block = pool->free_list;
    pool->free_list = block->next;

    return block->data;
}

void memory_pool_free(memory_pool_t* pool, void* ptr) {
    if (!ptr || !pool) return;

    // Find the block header from the data pointer
    char* data_ptr = (char*)ptr;
    memory_block_t* block = (memory_block_t*)(data_ptr - sizeof(memory_block_t));

    // Make sure this pointer is actually from our pool
    char* block_ptr = (char*)block;
    if (block_ptr < pool->pool_start ||
        block_ptr >= pool->pool_start + pool->pool_size) {
        KERROR("Invalid pointer returned to memory pool");
        return;
    }

    // Return to free list
    block->next = pool->free_list;
    pool->free_list = block;
}

void memory_pool_destroy(memory_pool_t* pool) {
    if (!pool) return;

    schedule_delay(1000); // Brief delay for cleanup

    if (pool->pool_start) {
        kfree_tracked(pool->pool_start);
    }
    kfree_tracked(pool);
}

// ============================================================================
// SMART POINTERS
// ============================================================================

smart_ptr_t make_smart_ptr(void* ptr, cleanup_func_t cleanup) {
    smart_ptr_t sp = { .ptr = ptr, .cleanup = cleanup };
    return sp;
}

void smart_ptr_cleanup(smart_ptr_t* sp) {
    if (sp && sp->ptr && sp->cleanup) {
        sp->cleanup(sp->ptr);
        sp->ptr = NULL;
        sp->cleanup = NULL;
    }
}

void auto_kfree(void* ptr) {
    kfree_tracked(ptr);
}

void auto_smart_cleanup(smart_ptr_t* sp) {
    smart_ptr_cleanup(sp);
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

const char* kernel_error_string(kernel_error_t error) {
    switch (error) {
        case KERNEL_SUCCESS: return "Success";
        case KERNEL_ERROR_INVALID_ARGUMENT: return "Invalid argument";
        case KERNEL_ERROR_NOT_FOUND: return "Not found";
        case KERNEL_ERROR_PERMISSION_DENIED: return "Permission denied";
        case KERNEL_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case KERNEL_ERROR_IO_ERROR: return "I/O error";
        case KERNEL_ERROR_NOT_IMPLEMENTED: return "Not implemented";
        case KERNEL_ERROR_TIMEOUT: return "Operation timed out";
        case KERNEL_ERROR_BUSY: return "Resource busy";
        case KERNEL_ERROR_EXISTS: return "Resource already exists";
        case KERNEL_ERROR_TOO_MANY: return "Too many resources";
        case KERNEL_ERROR_FILE_NOT_FOUND: return "File not found";
        case KERNEL_ERROR_DIRECTORY_NOT_EMPTY: return "Directory not empty";
        case KERNEL_ERROR_FILE_TOO_BIG: return "File too big";
        case KERNEL_ERROR_NO_SPACE: return "No space available";
        default: return "Unknown error";
    }
}
