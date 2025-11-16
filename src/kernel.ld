/* Base Kernel Linker Script for x86-64 */

ENTRY(_start)

SECTIONS {
    /* Kernel will be loaded at 0x100000 (1MB) by GRUB */
    /* Higher half mapping will be set up after entering long mode */
    . = 0x100000;

    /* Multiboot header must be in first 8KB */
    .multiboot : ALIGN(8) {
        KEEP(*(.multiboot))
        . = ALIGN(8);
    }

    /* Text section */
    .text : ALIGN(4K) {
        *(.text.start)
        *(.text)
        *(.text.*)
        . = ALIGN(4K);
    }

    /* Read-only data */
    .rodata : ALIGN(4K) {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4K);
    }

    /* Read-write data */
    .data : ALIGN(4K) {
        *(.data)
        *(.data.*)
        . = ALIGN(4K);
    }

    /* BSS (zero-initialized data) */
    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
        . = ALIGN(4K);
    }

    /* Kernel stack */
    .stack : ALIGN(16) {
        _kernel_stack_bottom = .;
        . += 64 * 1024;  /* 64KB kernel stack */
        _kernel_stack_top = .;
    }

    /* End of kernel image */
    _kernel_end = .;

    /* Discard debugging information */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}

/* Provide symbols for debugging and memory management */
_kernel_start = 0xFFFFFFFF80000000;
_kernel_size = _kernel_end - _kernel_start;
