; Interrupt Service Routines (ISRs) for x86_64
; Handles CPU exceptions and IRQs

BITS 64
SECTION .text

; External interrupt handler function
EXTERN interrupt_handler

; Macro to define ISR with no error code
%macro ISR_NOERRCODE 1
isr%1:
    cli                         ; Disable interrupts
    push byte 0                 ; Push dummy error code
    push byte %1                ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro to define ISR with error code
%macro ISR_ERRCODE 1
isr%1:
    cli                         ; Disable interrupts
    push byte %1                ; Push interrupt number
    jmp isr_common_stub
%endmacro

; CPU exception ISRs (0-31)
ISR_NOERRCODE 0         ; Division by zero
ISR_NOERRCODE 1         ; Debug
ISR_NOERRCODE 2         ; Non-maskable interrupt
ISR_NOERRCODE 3         ; Breakpoint
ISR_NOERRCODE 4         ; Overflow
ISR_NOERRCODE 5         ; Bound range exceeded
ISR_NOERRCODE 6         ; Invalid opcode
ISR_NOERRCODE 7         ; Device not available
ISR_ERRCODE   8         ; Double fault
ISR_NOERRCODE 9         ; Coprocessor segment overrun
ISR_ERRCODE   10        ; Invalid TSS
ISR_ERRCODE   11        ; Segment not present
ISR_ERRCODE   12        ; Stack segment fault
ISR_ERRCODE   13        ; General protection fault
ISR_ERRCODE   14        ; Page fault
ISR_NOERRCODE 15        ; Reserved
ISR_NOERRCODE 16        ; x87 FPU error
ISR_ERRCODE   17        ; Alignment check
ISR_NOERRCODE 18        ; Machine check
ISR_NOERRCODE 19        ; SIMD floating point exception
ISR_NOERRCODE 20        ; Virtualization exception
ISR_NOERRCODE 21        ; Reserved
ISR_NOERRCODE 22        ; Reserved
ISR_NOERRCODE 23        ; Reserved
ISR_NOERRCODE 24        ; Reserved
ISR_NOERRCODE 25        ; Reserved
ISR_NOERRCODE 26        ; Reserved
ISR_NOERRCODE 27        ; Reserved
ISR_NOERRCODE 28        ; Reserved
ISR_NOERRCODE 29        ; Reserved
ISR_NOERRCODE 30        ; Reserved
ISR_NOERRCODE 31        ; Reserved

; IRQ ISRs (32-47 after PIC remapping)
ISR_NOERRCODE 32        ; IRQ 0 - Timer
ISR_NOERRCODE 33        ; IRQ 1 - Keyboard
ISR_NOERRCODE 34        ; IRQ 2 - Cascade
ISR_NOERRCODE 35        ; IRQ 3 - COM2
ISR_NOERRCODE 36        ; IRQ 4 - COM1
ISR_NOERRCODE 37        ; IRQ 5 - LPT2
ISR_NOERRCODE 38        ; IRQ 6 - Floppy
ISR_NOERRCODE 39        ; IRQ 7 - LPT1
ISR_NOERRCODE 40        ; IRQ 8 - RTC
ISR_NOERRCODE 41        ; IRQ 9 - ACPI
ISR_NOERRCODE 42        ; IRQ 10 - Reserved
ISR_NOERRCODE 43        ; IRQ 11 - Reserved
ISR_NOERRCODE 44        ; IRQ 12 - PS/2 Mouse
ISR_NOERRCODE 45        ; IRQ 13 - FPU
ISR_NOERRCODE 46        ; IRQ 14 - Primary ATA
ISR_NOERRCODE 47        ; IRQ 15 - Secondary ATA

; System call interrupt (int 0x80)
ISR_NOERRCODE 128       ; System call

; Common ISR stub
isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Save segment registers
    mov ax, ds
    push ax
    mov ax, es
    push ax
    mov ax, fs
    push ax
    mov ax, gs
    push ax

    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Pass pointer to interrupt frame
    mov rdi, rsp
    call interrupt_handler

    ; Restore segment registers
    pop ax
    mov gs, ax
    pop ax
    mov fs, ax
    pop ax
    mov es, ax
    pop ax
    mov ds, ax

    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Clean up error code and interrupt number
    add rsp, 16

    ; Re-enable interrupts and return
    sti
    iretq

; ISR handler table (only defined ISRs)
GLOBAL isr_table
isr_table:
    ; CPU exceptions and IRQs (0-47)
    %assign i 0
    %rep 48
        dq isr%+i
    %assign i i+1
    %endrep

    ; Skip unused interrupts (48-127)
    ; Will be filled with zeros or null pointers

    ; System call (128)
    dq isr128

    ; Fill the rest of potential IDT entries with null
    times (256-49) dq 0
