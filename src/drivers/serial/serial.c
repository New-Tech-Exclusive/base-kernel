#include "kernel.h"
#include "io.h"

/*
 * Basic Serial Port Driver for x86
 * Provides output to COM1 for debugging
 */

#define COM1_PORT 0x3f8

/* Initialize the serial port */
void serial_init(void)
{
    /* Disable interrupts */
    outb(COM1_PORT + 1, 0x00);

    /* Set divisor latch access bit */
    outb(COM1_PORT + 3, 0x80);

    /* Set divisor (low byte) - 115200 baud */
    outb(COM1_PORT + 0, 0x01);

    /* Set divisor (high byte) */
    outb(COM1_PORT + 1, 0x00);

    /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 3, 0x03);

    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1_PORT + 2, 0xC7);

    /* Enable IRQs, RTS/DSR set */
    outb(COM1_PORT + 4, 0x0B);
}

/* Check if serial port is ready to transmit */
static int serial_is_transmit_empty(void)
{
    return inb(COM1_PORT + 5) & 0x20;
}

/* Write a character to serial port */
void serial_write(char c)
{
    while (!serial_is_transmit_empty());
    outb(COM1_PORT, c);
}

/* Write a string to serial port */
void serial_write_string(const char* str)
{
    while (*str) {
        serial_write(*str++);
    }
}
