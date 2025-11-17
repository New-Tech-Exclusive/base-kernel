#include "kernel.h"
#include "io.h"

/*
 * Programmable Interrupt Controller (PIC) driver
 * Remaps IRQs to avoid conflicts with CPU exceptions
 */

// PIC I/O ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// PIC commands
#define PIC_EOI         0x20    // End Of Interrupt
#define PIC_INIT        0x11    // Initialize command
#define PIC_8086        0x01    // 8086 mode

// IRQ offsets (remap to avoid CPU exceptions 0-31)
#define PIC1_OFFSET     0x20    // IRQ 0-7 -> INT 0x20-0x27
#define PIC2_OFFSET     0x28    // IRQ 8-15 -> INT 0x28-0x2F

// Initialize the PICs
void pic_init(void)
{
    KINFO("Initializing PICs...");

    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Start initialization sequence for PIC1
    outb(PIC1_COMMAND, PIC_INIT);
    io_wait();

    // Start initialization sequence for PIC2
    outb(PIC2_COMMAND, PIC_INIT);
    io_wait();

    // Set PIC1 vector offset
    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();

    // Set PIC2 vector offset
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    // Tell PIC2 it's cascaded from PIC1
    outb(PIC1_DATA, 0x04);
    io_wait();

    // Tell PIC1 there's a PIC2 at IRQ2
    outb(PIC2_DATA, 0x02);
    io_wait();

    // Set 8086 mode for both PICs
    outb(PIC1_DATA, PIC_8086);
    io_wait();
    outb(PIC2_DATA, PIC_8086);
    io_wait();

    // Restore saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    KINFO("PICs initialized successfully");
}

// Send End Of Interrupt signal to PIC
void pic_eoi(uint8_t irq)
{
    if (irq >= 8) {
        // Send EOI to PIC2
        outb(PIC2_COMMAND, PIC_EOI);
    }

    // Always send EOI to PIC1
    outb(PIC1_COMMAND, PIC_EOI);
}

// Mask an IRQ (disable)
void pic_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}

// Unmask an IRQ (enable)
void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

// Disable all IRQs
void pic_disable(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

// Enable all IRQs
void pic_enable(void)
{
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}
