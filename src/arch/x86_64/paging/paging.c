#include "kernel.h"

/*
 * 4-level paging implementation for x86-64
 * Implements virtual memory management
 */

// Page table entry flags
#define PTE_PRESENT       0x001
#define PTE_WRITABLE      0x002
#define PTE_USER          0x004
#define PTE_ACCESSED      0x020
#define PTE_DIRTY         0x040
#define PTE_PAGE_SIZE     0x080
#define PTE_GLOBAL        0x100
#define PTE_PAT           0x080
#define PTE_NX            (1ULL << 63)

// Page table entry structure (64-bit)
typedef uint64_t pte_t;

// Page table structures (all are pointers to pte_t arrays)
typedef pte_t* page_table_t;

// Page table pointers
static pte_t* pml4;

// Physical address of current page table
static uintptr_t current_page_table;

// Memory layout constants
#define KERNEL_PML4_INDEX   511  // Higher half (0xFFFFFFFF80000000)
#define KERNEL_PDPT_INDEX   510

// Forward declarations
static void paging_create_kernel_tables(void);
static pte_t* paging_alloc_page_table(void);

// Initialize paging (bootloader already set up identity mapping)
void paging_init(void)
{
    KINFO("Verifying paging setup...");

    // Bootloader already set up identity mapping and enabled long mode
    // We just need to verify it's working and get the current state

    // Get current page table base from CR3
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4 = (pte_t*)cr3;

    // For now, just verify paging is enabled - we'll set up higher-half kernel
    // mapping once we have proper memory allocation (after kheap is ready)
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    if (!(cr0 & (1 << 31))) {
        KWARN("WARNING: Paging not enabled by bootloader");
    } else {
        KINFO("Paging enabled and verified");
    }

    KINFO("Paging setup verified (identity mapping active)");
}

// Create kernel page tables
static void paging_create_kernel_tables(void)
{
    // Allocate and initialize PML4
    pml4 = paging_alloc_page_table();
    memset(pml4, 0, PAGE_SIZE);

    // For higher-half kernel: map 0x00000000 to physical 0x00000000
    // and 0xFFFFFFFF80000000 to physical 0x00000000
    // This creates identity mapping for lower 1GB

    // Create PDPT entry for identity mapping (entry 0)
    pte_t* pdpt_lower = paging_alloc_page_table();
    memset(pdpt_lower, 0, PAGE_SIZE);

    // Create PD entry for lower PDPT
    pte_t* pd_lower = paging_alloc_page_table();
    memset(pd_lower, 0, PAGE_SIZE);

    // Identity map first 1GB of physical memory (2MB pages for simplicity)
    pdpt_lower[0] = ((uintptr_t)pd_lower) | PTE_PRESENT | PTE_WRITABLE;

    // Create 512 page directory entries (2MB each = 1GB total)
    for (int i = 0; i < 512; i++) {
        pd_lower[i] = (i * 0x200000ULL) | PTE_PRESENT | PTE_WRITABLE | PTE_PAGE_SIZE;
    }

    // Identity map first 1GB
    pml4[0] = ((uintptr_t)pdpt_lower) | PTE_PRESENT | PTE_WRITABLE;

    // Map kernel higher-half (0xFFFFFFFF80000000) to physical 0x00000000
    pml4[KERNEL_PML4_INDEX] = ((uintptr_t)pdpt_lower) | PTE_PRESENT | PTE_WRITABLE;

    current_page_table = (uintptr_t)pml4;
}

/*
 * Virtual Memory Manager functions
 */

// Map a virtual page to a physical page
bool vmm_map_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint32_t flags)
{
    // Align addresses to page boundaries
    virtual_addr &= ~(PAGE_SIZE - 1);
    physical_addr &= ~(PAGE_SIZE - 1);

    // Get page table indices
    uint16_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint16_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    uint16_t pd_index = (virtual_addr >> 21) & 0x1FF;
    uint16_t pt_index = (virtual_addr >> 12) & 0x1FF;

    // Ensure PML4 entry exists
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        page_table_t pdpt = paging_alloc_page_table();
        if (!pdpt) return false;
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_index] = ((uintptr_t)pdpt) | PTE_PRESENT | PTE_WRITABLE;
    }

    // Get PDPT
    page_table_t pdpt = (page_table_t)(pml4[pml4_index] & ~0xFFFULL);
    if (!(pdpt[pdpt_index] & PTE_PRESENT)) {
        page_table_t pd = paging_alloc_page_table();
        if (!pd) return false;
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_index] = ((uintptr_t)pd) | PTE_PRESENT | PTE_WRITABLE;
    }

    // Get PD
    page_table_t pd = (page_table_t)(pdpt[pdpt_index] & ~0xFFFULL);
    if (!(pd[pd_index] & PTE_PRESENT)) {
        page_table_t pt = paging_alloc_page_table();
        if (!pt) return false;
        memset(pt, 0, PAGE_SIZE);
        pd[pd_index] = ((uintptr_t)pt) | PTE_PRESENT | PTE_WRITABLE;
    }

    // Get PT and set PTE
    page_table_t pt = (page_table_t)(pd[pd_index] & ~0xFFFULL);
    pt[pt_index] = (physical_addr) | flags | PTE_PRESENT;

    // Flush TLB for this page
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr));

    return true;
}

// Unmap a virtual page
bool vmm_unmap_page(uintptr_t virtual_addr)
{
    virtual_addr &= ~(PAGE_SIZE - 1);

    uint16_t pml4_index = (virtual_addr >> 39) & 0x1FF;

    if (!(pml4[pml4_index] & PTE_PRESENT))
        return false;

    page_table_t pdpt = (page_table_t)(pml4[pml4_index] & ~0xFFFULL);

    uint16_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    if (!(pdpt[pdpt_index] & PTE_PRESENT))
        return false;

    page_table_t pd = (page_table_t)(pdpt[pdpt_index] & ~0xFFFULL);

    uint16_t pd_index = (virtual_addr >> 21) & 0x1FF;
    if (!(pd[pd_index] & PTE_PRESENT))
        return false;

    page_table_t pt = (page_table_t)(pd[pd_index] & ~0xFFFULL);

    uint16_t pt_index = (virtual_addr >> 12) & 0x1FF;
    if (!(pt[pt_index] & PTE_PRESENT))
        return false;

    // Clear the entry
    pt[pt_index] = 0;

    // Flush TLB
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr));

    return true;
}

// Allocate a new page table page using PMM
static pte_t* paging_alloc_page_table(void)
{
    uintptr_t phys_addr = pmm_alloc_pages(1);
    if (!phys_addr) {
        KERROR("Failed to allocate page table page");
        return NULL;
    }

    // The page is allocated from the identity-mapped region during early boot
    // so physical addresses are directly usable as virtual addresses initially
    return (pte_t*)phys_addr;
}

// Virtual to physical address translation (for debugging)
uintptr_t paging_get_physical_address(uintptr_t virtual_addr)
{
    uint16_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint16_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    uint16_t pd_index = (virtual_addr >> 21) & 0x1FF;
    uint16_t pt_index = (virtual_addr >> 12) & 0x1FF;

    if (!(pml4[pml4_index] & PTE_PRESENT))
        return 0;

    page_table_t pdpt = (page_table_t)(pml4[pml4_index] & ~0xFFFULL);
    if (!(pdpt[pdpt_index] & PTE_PRESENT))
        return 0;

    if (pdpt[pdpt_index] & PTE_PAGE_SIZE) {
        // 1GB page
        return (pdpt[pdpt_index] & ~0x3FFFFFFFULL) + (virtual_addr & 0x3FFFFFFFULL);
    }

    page_table_t pd = (page_table_t)(pdpt[pdpt_index] & ~0xFFFULL);
    if (!(pd[pd_index] & PTE_PRESENT))
        return 0;

    if (pd[pd_index] & PTE_PAGE_SIZE) {
        // 2MB page
        return (pd[pd_index] & ~0x1FFFFFULL) + (virtual_addr & 0x1FFFFFULL);
    }

    page_table_t pt = (page_table_t)(pd[pd_index] & ~0xFFFULL);
    if (!(pt[pt_index] & PTE_PRESENT))
        return 0;

    // 4KB page
    return (pt[pt_index] & ~0xFFFULL) + (virtual_addr & 0xFFFULL);
}
