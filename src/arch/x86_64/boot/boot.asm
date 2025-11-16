; Base Kernel Boot Code for x86-64
; This code is loaded by GRUB using the Multiboot 2 specification

; Multiboot 2 header
section .multiboot
align 8
multiboot_header_start:
    dd 0xE85250D6                ; Magic number
    dd 0                         ; Architecture (0 = i386 protected mode)
    dd multiboot_header_end - multiboot_header_start ; Header length
    dd -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header_start)) ; Checksum

    ; Information request tag
    dw 1                         ; Type
    dw 0                         ; Flags
    dd 12                        ; Size
    dd 0                         ; Request tag (Required end tag)

    ; Framebuffer tag
    dw 5                         ; Type
    dw 0                         ; Flags
    dd 20                        ; Size
    dd 0                         ; Width (0 = no preference)
    dd 0                         ; Height (0 = no preference)
    dd 0                         ; Depth (0 = text mode)

    ; End tag
    dw 0                         ; Type
    dw 0                         ; Flags
    dd 8                         ; Size
multiboot_header_end:

; Kernel entry point
section .text.start
global _start
extern kernel_early_init

BITS 32
_start:
    ; GRUB has already set up a 32-bit protected mode environment:
    ; - We're in 32-bit protected mode
    ; - A20 gate is enabled
    ; - EAX contains the multiboot magic number (0x36D76289)
    ; - EBX contains the multiboot info structure pointer

    ; Save multiboot info (kernel loaded at 0x100000, so absolute addressing works)
    mov [multiboot_magic], eax
    mov [multiboot_info], ebx

    ; Set up a minimal stack in lower memory
    mov esp, stack_top

    ; Check if we got the expected multiboot magic
    cmp eax, 0x36D76289
    jne .error

    ; Basic CPU setup
    call setup_long_mode

    ; Jump to C code in long mode
    lgdt [gdt64.pointer]
    jmp gdt64.code:kmain64

.error:
    ; Hang if multiboot magic is wrong
    cli
    hlt
    jmp .error

; Set up identity-mapped paging for kernel
setup_long_mode:
    ; Disable paging temporarily (should already be off)
    mov eax, cr0
    and eax, ~0x80000000
    mov cr0, eax

    ; Set up 4-level paging with identity mapping for low 2MB
    ; This allows the kernel to run in the first 2MB while we set up higher-half

    ; Clear page tables
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov edi, cr3

    ; Setup Page Map Level 4 (PML4)
    mov DWORD [edi], 0x2000 | 3        ; Present + Write + User

    ; Setup Page Directory Pointer Table (PDPT)
    add edi, 0x1000
    mov DWORD [edi], 0x3000 | 3        ; Present + Write + User

    ; Setup Page Directory (PD)
    add edi, 0x1000
    mov DWORD [edi], 0x4000 | 3        ; Present + Write + User

    ; Setup Page Table with identity mapping (first 2MB)
    add edi, 0x1000
    mov ebx, 3                   ; Present + Write flags
    mov ecx, 512                 ; 512 entries for 2MB

.fill_page_table:
    mov DWORD [edi], ebx
    add ebx, 4096                ; Next physical page
    add edi, 8                   ; Next entry
    loop .fill_page_table

    ; Enable Physical Address Extension (PAE)
    mov eax, cr4
    or  eax, 1 << 5
    mov cr4, eax

    ; Load CR3 with page table base
    mov eax, 0x1000
    mov cr3, eax

    ; Enable Long Mode in EFER
    mov ecx, 0xC0000080          ; EFER MSR
    rdmsr
    or  eax, 1 << 8               ; Set LME bit
    wrmsr

    ; Enable paging
    mov eax, cr0
    or  eax, 1 << 31              ; Set PG bit
    mov cr0, eax

    ret

; 64-bit kernel entry
BITS 64
kmain64:
    ; Set up 64-bit GDT
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Set up proper stack (should be safe in identity mapped area)
    mov rsp, stack_top

    ; Jump to kernel_early_init (now in 64-bit mode)
    call kernel_early_init

    ; Should never return
    cli
    hlt

; Temporary stack (2MB should be plenty for early boot)
section .bss
align 16
stack_bottom:
    resb 8192                      ; 8KB stack
stack_top:

; Multiboot info storage (used by PMM later)
section .data
global multiboot_magic
global multiboot_info
multiboot_magic: dd 0
multiboot_info: dd 0

; GDT for 64-bit mode
section .rodata
align 16
gdt64:
    dq 0                        ; Null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; Code segment
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41) ; Data segment

.pointer:
    dw $ - gdt64 - 1             ; Limit
    dq gdt64                     ; Base
