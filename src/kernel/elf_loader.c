/*
 * ELF64 Loader
 * Loads and executes ELF64 userspace programs
 */

#include "kernel.h"
#include "elf.h"

// User space memory layout
#define USER_STACK_TOP   0x7FFFFFFFE000ULL  // Top of user stack
#define USER_STACK_SIZE  (2 * 1024 * 1024)  // 2MB stack
#define USER_CODE_BASE   0x400000ULL        // Base address for code

// Validate ELF header
int elf_validate(const void* elf_data, size_t size) {
    if (!elf_data || size < sizeof(elf64_ehdr_t)) {
        KERROR("ELF: Invalid data or size too small");
        return -1;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)elf_data;

    // Check magic number
    if (ehdr->e_ident[EI_MAG0] != 0x7F ||
        ehdr->e_ident[EI_MAG1] != 'E' ||
        ehdr->e_ident[EI_MAG2] != 'L' ||
        ehdr->e_ident[EI_MAG3] != 'F') {
        KERROR("ELF: Invalid magic number");
        return -1;
    }

    // Check for 64-bit ELF
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        KERROR("ELF: Not a 64-bit ELF");
        return -1;
    }

    // Check endianness (little-endian)
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        KERROR("ELF: Not little-endian");
        return -1;
    }

    // Check version
    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        KERROR("ELF: Invalid version");
        return -1;
    }

    // Check machine type (x86-64)
    if (ehdr->e_machine != EM_X86_64) {
        KERROR("ELF: Not x86-64 architecture");
        return -1;
    }

    // Check file type (executable)
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        KERROR("ELF: Not an executable or shared object");
        return -1;
    }

    KDEBUG("ELF: Header validation passed");
    return 0;
}

// Get entry point from ELF
uint64_t elf_get_entry(const void* elf_data) {
    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)elf_data;
    return ehdr->e_entry;
}

// Load ELF program into memory
int elf_load(const void* elf_data, size_t size, uint64_t* entry_point) {
    // Validate first
    if (elf_validate(elf_data, size) != 0) {
        return -1;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)elf_data;
    
    // Get program headers
    const elf64_phdr_t* phdr = (const elf64_phdr_t*)((uintptr_t)elf_data + ehdr->e_phoff);
    
    KINFO("ELF: Loading program with %d segments", ehdr->e_phnum);
    KINFO("ELF: Entry point at 0x%lx", ehdr->e_entry);

    // Load each PT_LOAD segment
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }

        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t offset = phdr[i].p_offset;
        uint32_t flags = phdr[i].p_flags;

        KDEBUG("ELF: Loading segment %d:", i);
        KDEBUG("  Virtual address: 0x%lx", vaddr);
        KDEBUG("  File size: %lu bytes", filesz);
        KDEBUG("  Memory size: %lu bytes", memsz);
        KDEBUG("  Flags: %c%c%c",
               (flags & PF_R) ? 'R' : '-',
               (flags & PF_W) ? 'W' : '-',
               (flags & PF_X) ? 'X' : '-');

        // Allocate memory for this segment
        // Calculate number of pages needed
        uint64_t start_page = vaddr & ~0xFFFULL;
        uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;
        size_t num_pages = (end_page - start_page) / PAGE_SIZE;

        KDEBUG("  Allocating %lu pages from 0x%lx to 0x%lx", 
               num_pages, start_page, end_page);

        // Allocate physical pages
        for (size_t pg = 0; pg < num_pages; pg++) {
            uintptr_t phys_page = pmm_alloc_page();
            if (!phys_page) {
                KERROR("ELF: Failed to allocate physical page");
                return -1;
            }

            uint64_t virt_page = start_page + (pg * PAGE_SIZE);
            
            // Map page with appropriate permissions
            uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
            if (flags & PF_W) {
                page_flags |= PAGE_WRITABLE;
            }
            // Note: Execute permission is implicit (no NX bit set)

            if (vmm_map_page(virt_page, phys_page, page_flags) != 0) {
                KERROR("ELF: Failed to map page at 0x%lx", virt_page);
                return -1;
            }
        }

        // Copy data from file to memory
        if (filesz > 0) {
            const uint8_t* src = (const uint8_t*)elf_data + offset;
            uint8_t* dst = (uint8_t*)vaddr;
            memcpy(dst, src, filesz);
        }

        // Zero out BSS (if memsz > filesz)
        if (memsz > filesz) {
            uint8_t* bss = (uint8_t*)(vaddr + filesz);
            memset(bss, 0, memsz - filesz);
        }

        KDEBUG("  Segment loaded successfully");
    }

    // Set up user stack
    KDEBUG("ELF: Setting up user stack");
    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    size_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;

    for (size_t pg = 0; pg < stack_pages; pg++) {
        uintptr_t phys_page = pmm_alloc_page();
        if (!phys_page) {
            KERROR("ELF: Failed to allocate stack page");
            return -1;
        }

        uint64_t virt_page = stack_bottom + (pg * PAGE_SIZE);
        if (vmm_map_page(virt_page, phys_page, 
                        PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
            KERROR("ELF: Failed to map stack page");
            return -1;
        }
    }

    // Zero out stack
    memset((void*)stack_bottom, 0, USER_STACK_SIZE);

    KINFO("ELF: Program loaded successfully");
    KINFO("ELF: Code base: 0x%lx", USER_CODE_BASE);
    KINFO("ELF: Stack: 0x%lx - 0x%lx", stack_bottom, USER_STACK_TOP);

    if (entry_point) {
        *entry_point = ehdr->e_entry;
    }

    return 0;
}

// Execute loaded ELF program (transition to userspace)
int elf_exec(uint64_t entry_point) {
    KINFO("ELF: Executing program at 0x%lx", entry_point);
    
    // This will be implemented when we add user mode transition
    // For now, return success
    KWARN("ELF: User mode execution not yet implemented");
    
    return 0;
}
