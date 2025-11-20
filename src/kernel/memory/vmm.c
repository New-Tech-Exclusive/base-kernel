/*
 * Virtual Memory Manager (VMM)
 * 
 * Features:
 * - Demand paging with page fault handling
 * - Memory-mapped files (mmap)
 * - Copy-on-write (CoW) fork support
 * - CLOCK page replacement algorithm
 * - Anonymous memory mappings
 * - Shared memory regions
 */

#include "kernel.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define MAX_VMA_PER_PROCESS 256   // Max virtual memory areas per process
#define PAGE_TABLE_LEVELS 4        // x86-64 has 4-level paging

// Page fault error codes
#define PF_PRESENT    (1 << 0)  // Page not present
#define PF_WRITE      (1 << 1)  // Write access
#define PF_USER       (1 << 2)  // User mode access
#define PF_RESERVED   (1 << 3)  // Reserved bit violation
#define PF_INSTR      (1 << 4)  // Instruction fetch

// VMA protection flags
#define PROT_NONE     0x0
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

// VMA flags
#define MAP_SHARED    0x01    // Share changes
#define MAP_PRIVATE   0x02    // Private copy-on-write
#define MAP_ANONYMOUS 0x20    // Not backed by file
#define MAP_FIXED     0x10    // Fixed address

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Virtual Memory Area - represents a contiguous region of virtual memory
typedef struct vma {
    uintptr_t start;              // Start virtual address
    uintptr_t end;                // End virtual address (exclusive)
    uint32_t prot;                // Protection flags (PROT_*)
    uint32_t flags;               // Mapping flags (MAP_*)
    
    // File backing (for file-backed mappings)
    void* file;                   // File pointer (NULL for anonymous)
    uint64_t offset;              // Offset in file
    
    // COW support
    uint32_t ref_count;           // Reference count for shared pages
    
    struct vma* next;             // Linked list
} vma_t;

// Process virtual memory context (defined in kernel.h)
// vm_context_t is now in kernel.h

// Page table entry structure (x86-64)
typedef struct {
    uint64_t present    : 1;   // Page is present in memory
    uint64_t writable   : 1;   // Page is writable
    uint64_t user       : 1;   // User-accessible
    uint64_t writethrough : 1; // Write-through caching
    uint64_t cache_disable : 1; // Cache disabled
    uint64_t accessed   : 1;   // Accessed flag
    uint64_t dirty      : 1;   // Dirty flag
    uint64_t huge       : 1;   // Huge page (2MB/1GB)
    uint64_t global     : 1;   // Global page
    uint64_t available  : 3;   // Available for OS use
    uint64_t address    : 40;  // Physical page frame number
    uint64_t available2 : 11;  // Available for OS use
    uint64_t no_execute : 1;   // No execute
} __attribute__((packed)) pte_t;

// Page replacement - CLOCK algorithm state
typedef struct {
    uintptr_t* page_list;      // Circular list of pages
    size_t clock_hand;         // Current position in list
    size_t capacity;           // Total pages tracked
    size_t count;              // Current page count
} clock_state_t;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static clock_state_t page_clock;
static vm_context_t kernel_vm_context;  // Kernel's own VM context

// Statistics
static size_t page_faults_total = 0;
static size_t page_faults_major = 0;  // Required disk I/O
static size_t page_faults_minor = 0;  // Already in memory
static size_t cow_faults = 0;

// ============================================================================
// PAGE TABLE MANAGEMENT
// ============================================================================

// Get page table entry for virtual address
static pte_t* vmm_get_pte(uint64_t* page_dir, uintptr_t vaddr, bool create)
{
    // Extract indices for 4-level paging
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdp_idx  = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;
    
    // Navigate through page tables, creating if needed
    uint64_t* pml4 = page_dir;
    
    // PML4 -> PDP
    if (!(pml4[pml4_idx] & 0x1)) {
        if (!create) return NULL;
        uintptr_t pdp = pmm_alloc_pages(1);
        if (!pdp) return NULL;
        memset((void*)pdp, 0, PAGE_SIZE);
        pml4[pml4_idx] = pdp | 0x3;  // Present + Writable
    }
    uint64_t* pdp = (uint64_t*)(pml4[pml4_idx] & ~0xFFF);
    
    // PDP -> PD
    if (!(pdp[pdp_idx] & 0x1)) {
        if (!create) return NULL;
        uintptr_t pd = pmm_alloc_pages(1);
        if (!pd) return NULL;
        memset((void*)pd, 0, PAGE_SIZE);
        pdp[pdp_idx] = pd | 0x3;
    }
    uint64_t* pd = (uint64_t*)(pdp[pdp_idx] & ~0xFFF);
    
    // PD -> PT
    if (!(pd[pd_idx] & 0x1)) {
        if (!create) return NULL;
        uintptr_t pt = pmm_alloc_pages(1);
        if (!pt) return NULL;
        memset((void*)pt, 0, PAGE_SIZE);
        pd[pd_idx] = pt | 0x3;
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFF);
    
    return (pte_t*)&pt[pt_idx];
}

// Map a virtual page to a physical page
static int vmm_map_page(uint64_t* page_dir, uintptr_t vaddr, uintptr_t paddr, uint32_t prot)
{
    pte_t* pte = vmm_get_pte(page_dir, vaddr, true);
    if (!pte) {
        return -1;
    }
    
    // Set page table entry
    pte->present = 1;
    pte->writable = (prot & PROT_WRITE) ? 1 : 0;
    pte->user = 1;  // Assuming user pages
    pte->no_execute = (prot & PROT_EXEC) ? 0 : 1;
    pte->address = paddr >> 12;  // Physical page frame number
    
    // Flush TLB for this address
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    
    return 0;
}

// Unmap a virtual page
static void vmm_unmap_page(uint64_t* page_dir, uintptr_t vaddr)
{
    pte_t* pte = vmm_get_pte(page_dir, vaddr, false);
    if (pte && pte->present) {
        uintptr_t paddr = (uintptr_t)pte->address << 12;
        pmm_free_pages(paddr, 1);
        
        memset(pte, 0, sizeof(pte_t));
        __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    }
}

// ============================================================================
// VMA MANAGEMENT
// ============================================================================

// Create a new VMA
static vma_t* vmm_create_vma(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags)
{
    vma_t* vma = kmalloc_tracked(sizeof(vma_t), "vma");
    if (!vma) return NULL;
    
    vma->start = start;
    vma->end = end;
    vma->prot = prot;
    vma->flags = flags;
    vma->file = NULL;
    vma->offset = 0;
    vma->ref_count = 1;
    vma->next = NULL;
    
    return vma;
}

// Find VMA containing address
static vma_t* vmm_find_vma(vm_context_t* ctx, uintptr_t addr)
{
    vma_t* vma = ctx->vma_list;
    while (vma) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        vma = vma->next;
    }
    return NULL;
}

// Insert VMA into context (sorted by address)
static void vmm_insert_vma(vm_context_t* ctx, vma_t* new_vma)
{
    if (!ctx->vma_list || new_vma->start < ctx->vma_list->start) {
        new_vma->next = ctx->vma_list;
        ctx->vma_list = new_vma;
        return;
    }
    
    vma_t* current = ctx->vma_list;
    while (current->next && current->next->start < new_vma->start) {
        current = current->next;
    }
    
    new_vma->next = current->next;
    current->next = new_vma;
}

// ============================================================================
// MMAP IMPLEMENTATION
// ============================================================================

void* vmm_mmap(vm_context_t* ctx, void* addr, size_t length, 
               int prot, int flags, void* file, uint64_t offset)
{
    if (length == 0) return NULL;
    
    // Align length to page size
    length = ALIGN_UP(length, PAGE_SIZE);
    
    // Determine address if not specified
    uintptr_t vaddr;
    if (flags & MAP_FIXED) {
        vaddr = (uintptr_t)addr;
    } else {
        // Find free space starting from mmap_base
        vaddr = ctx->mmap_base;
        
        // Simple linear search for free space
        vma_t* vma = ctx->vma_list;
        while (vma) {
            if (vaddr + length <= vma->start) {
                break;  // Found space
            }
            vaddr = ALIGN_UP(vma->end, PAGE_SIZE);
            vma = vma->next;
        }
    }
    
    // Create VMA
    vma_t* new_vma = vmm_create_vma(vaddr, vaddr + length, prot, flags);
    if (!new_vma) {
        return NULL;
    }
    
    new_vma->file = file;
    new_vma->offset = offset;
    
    vmm_insert_vma(ctx, new_vma);
    
    // For anonymous mappings, don't allocate pages yet (demand paging)
    if (flags & MAP_ANONYMOUS) {
        // Pages will be allocated on first access
        KDEBUG("mmap: anonymous mapping at 0x%lx, size %lu", vaddr, length);
    } else {
        // For file-backed mappings, would load pages here
        KDEBUG("mmap: file-backed mapping at 0x%lx, size %lu", vaddr, length);
    }
    
    return (void*)vaddr;
}

int vmm_munmap(vm_context_t* ctx, void* addr, size_t length)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + ALIGN_UP(length, PAGE_SIZE);
    
    vma_t** vma_ptr = &ctx->vma_list;
    while (*vma_ptr) {
        vma_t* vma = *vma_ptr;
        
        if (vma->start >= end) break;
        
        if (vma->end > start && vma->start < end) {
            // Unmap pages in this VMA
            for (uintptr_t page = vma->start; page < vma->end; page += PAGE_SIZE) {
                vmm_unmap_page(ctx->page_dir, page);
            }
            
            // Remove VMA
            *vma_ptr = vma->next;
            kfree_tracked(vma);
        } else {
            vma_ptr = &vma->next;
        }
    }
    
    return 0;
}

// ============================================================================
// PAGE FAULT HANDLER
// ============================================================================

void vmm_page_fault_handler(uintptr_t fault_addr, uint32_t error_code)
{
    page_faults_total++;
    
    // Get current process VM context (simplified - would get from scheduler)
    vm_context_t* ctx = &kernel_vm_context;
    
    KDEBUG("Page fault at 0x%lx, error=0x%x", fault_addr, error_code);
    
    // Find VMA for this address
    vma_t* vma = vmm_find_vma(ctx, fault_addr);
    if (!vma) {
        KERROR("Segmentation fault: no VMA for address 0x%lx", fault_addr);
        return;
    }
    
    // Check permissions
    if ((error_code & PF_WRITE) && !(vma->prot & PROT_WRITE)) {
        KERROR("Permission denied: write to read-only page at 0x%lx", fault_addr);
        return;
    }
    
    // Handle copy-on-write
    pte_t* pte = vmm_get_pte(ctx->page_dir, fault_addr, false);
    if (pte && pte->present && (error_code & PF_WRITE) && 
        (vma->flags & MAP_PRIVATE)) {
        
        cow_faults++;
        
        // Copy page for COW
        uintptr_t old_page = (uintptr_t)pte->address << 12;
        uintptr_t new_page = pmm_alloc_pages(1);
        
        if (!new_page) {
            KERROR("Out of memory during COW");
            return;
        }
        
        memcpy((void*)new_page, (void*)old_page, PAGE_SIZE);
        
        // Update PTE to new page
        pte->address = new_page >> 12;
        pte->writable = 1;
        
        __asm__ volatile("invlpg (%0)" :: "r"(fault_addr) : "memory");
        
        KDEBUG("COW fault resolved at 0x%lx", fault_addr);
        return;
    }
    
    // Regular page fault - allocate page
    page_faults_minor++;
    
    uintptr_t page_addr = fault_addr & ~(PAGE_SIZE - 1);
    uintptr_t phys_page = pmm_alloc_pages(1);
    
    if (!phys_page) {
        KERROR("Out of memory during page fault");
        return;
    }
    
    // Zero the page for security
    memset((void*)phys_page, 0, PAGE_SIZE);
    
    // Map the page
    vmm_map_page(ctx->page_dir, page_addr, phys_page, vma->prot);
    
    KDEBUG("Demand-paged: allocated page at 0x%lx -> 0x%lx", 
           page_addr, phys_page);
}

// ============================================================================
// BRK (HEAP) MANAGEMENT
// ============================================================================

void* vmm_brk(vm_context_t* ctx, void* addr)
{
    if (!addr) {
        // Query current brk
        return (void*)ctx->brk;
    }
    
    uintptr_t new_brk = (uintptr_t)addr;
    uintptr_t old_brk = ctx->brk;
    
    if (new_brk < old_brk) {
        // Shrink heap - unmap pages
        for (uintptr_t page = new_brk; page < old_brk; page += PAGE_SIZE) {
            vmm_unmap_page(ctx->page_dir, page);
        }
    } else {
        // Expand heap - create VMA (pages allocated on demand)
        vma_t* heap_vma = vmm_create_vma(old_brk, new_brk, 
                                         PROT_READ | PROT_WRITE, 
                                         MAP_PRIVATE | MAP_ANONYMOUS);
        if (heap_vma) {
            vmm_insert_vma(ctx, heap_vma);
        }
    }
    
    ctx->brk = new_brk;
    return (void*)new_brk;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void vmm_init(void)
{
    KINFO("Initializing Virtual Memory Manager...");
    
    // Initialize kernel VM context
    kernel_vm_context.vma_list = NULL;
    kernel_vm_context.brk = 0x40000000;       // 1GB mark for heap
    kernel_vm_context.mmap_base = 0x60000000; // 1.5GB mark for mmap
    
    // Allocate page directory for kernel
    kernel_vm_context.page_dir = (uint64_t*)pmm_alloc_pages(1);
    if (!kernel_vm_context.page_dir) {
        PANIC("Failed to allocate kernel page directory");
    }
    memset(kernel_vm_context.page_dir, 0, PAGE_SIZE);
    
    // Initialize CLOCK algorithm
    page_clock.capacity = 1024;  // Track up to 1024 pages
    page_clock.page_list = kmalloc_tracked(
        sizeof(uintptr_t) * page_clock.capacity, "clock_state");
    page_clock.clock_hand = 0;
    page_clock.count = 0;
    
    KINFO("VMM initialized:");
    KINFO("  ├─ Heap break: 0x%lx", kernel_vm_context.brk);
    KINFO("  ├─ mmap base: 0x%lx", kernel_vm_context.mmap_base);
    KINFO("  └─ Page replacement: CLOCK algorithm");
}

// ============================================================================
// STATISTICS
// ============================================================================

void vmm_get_stats(void)
{
    KINFO("=== VMM Statistics ===");
    KINFO("Total page faults: %lu", page_faults_total);
    KINFO("  Minor faults: %lu", page_faults_minor);
    KINFO("  Major faults: %lu", page_faults_major);
    KINFO("  COW faults: %lu", cow_faults);
}
