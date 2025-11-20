/*
 * Virtual Memory Manager Header
 * Public API for VMM subsystem
 */

#ifndef VMM_H
#define VMM_H

#include "types.h"

// ============================================================================
// CONSTANTS
// ============================================================================

// Protection flags
#define PROT_NONE     0x0
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

// Mapping flags
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

// ============================================================================
// TYPES
// ============================================================================

typedef struct vm_context vm_context_t;
typedef struct vma vma_t;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialization
void vmm_init(void);

// Memory mapping
void* vmm_mmap(vm_context_t* ctx, void* addr, size_t length, 
               int prot, int flags, void* file, uint64_t offset);
int vmm_munmap(vm_context_t* ctx, void* addr, size_t length);

// Heap management (brk)
void* vmm_brk(vm_context_t* ctx, void* addr);

// Page fault handling
void vmm_page_fault_handler(uintptr_t fault_addr, uint32_t error_code);

// Statistics
void vmm_get_stats(void);

#endif /* VMM_H */
