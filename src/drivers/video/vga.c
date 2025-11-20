#include "kernel.h"

/*
 * Basic VGA Text Mode Driver for x86
 * Provides simple text output to screen
 */

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

/* VGA text buffer - starts at 0xB8000 in physical memory */
static volatile uint16_t* vga_buffer = (volatile uint16_t*)0xFFFFFFFF800B8000ULL;

/* VGA colors */
#define VGA_BLACK 0
#define VGA_BLUE 1
#define VGA_GREEN 2
#define VGA_CYAN 3
#define VGA_RED 4
#define VGA_MAGENTA 5
#define VGA_BROWN 6
#define VGA_LIGHT_GREY 7
#define VGA_DARK_GREY 8
#define VGA_LIGHT_BLUE 9
#define VGA_LIGHT_GREEN 10
#define VGA_LIGHT_CYAN 11
#define VGA_LIGHT_RED 12
#define VGA_LIGHT_MAGENTA 13
#define VGA_YELLOW 14
#define VGA_WHITE 15

/* Current cursor position */
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

/* VGA color byte */
static uint8_t vga_color = VGA_LIGHT_GREY | (VGA_BLACK << 4);

/* Scroll screen up */
static void vga_scroll(void)
{
    /* Move all lines up by one */
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            size_t src = y * VGA_WIDTH + x;
            size_t dst = (y - 1) * VGA_WIDTH + x;
            vga_buffer[dst] = vga_buffer[src];
        }
    }

    /* Clear last line */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        vga_buffer[index] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
}

/* Initialize VGA */
void vga_init(void)
{
    /* Clear screen */
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }

    cursor_x = 0;
    cursor_y = 0;
}

/* Set text color */
void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = fg | (bg << 4);
}

/* Put a character on screen */
void vga_putc(char c)
{
    /* Handle newline */
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
            vga_scroll();
        }
        return;
    }

    /* Handle carriage return */
    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    /* Put character at current cursor position */
    size_t index = cursor_y * VGA_WIDTH + cursor_x;
    vga_buffer[index] = (uint16_t)c | ((uint16_t)vga_color << 8);

    /* Move cursor */
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
            vga_scroll();
        }
    }
}

/* Put a string on screen */
void vga_puts(const char* str)
{
    while (*str) {
        vga_putc(*str++);
    }
}
