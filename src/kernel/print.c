#include "kernel.h"

/*
 * Basic printing functions for kernel debugging
 * Very simple printf-like implementation
 */

/* Simple printf-style output */
static void print_string(const char* str)
{
    while (*str) {
        serial_write(*str++);
    }
}

static void print_int(int num)
{
    if (num == 0) {
        serial_write('0');
        return;
    }

    if (num < 0) {
        serial_write('-');
        num = -num;
    }

    char buf[12];
    int i = 0;

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i > 0) {
        serial_write(buf[--i]);
    }
}

/* Debug output functions */
void kernel_debug(const char* fmt, ...)
{
    print_string("[DEBUG] ");
    print_string(fmt);
    print_string("\n");
}

void kernel_info(const char* fmt, ...)
{
    print_string("[INFO] ");
    print_string(fmt);
    print_string("\n");
}

void kernel_warn(const char* fmt, ...)
{
    print_string("[WARN] ");
    print_string(fmt);
    print_string("\n");
}

void kernel_error(const char* fmt, ...)
{
    print_string("[ERROR] ");
    print_string(fmt);
    print_string("\n");
}

/* Panic function */
__NORETURN void kernel_panic(const char* file, int line, const char* msg)
{
    print_string("KERNEL PANIC at ");
    print_string(file);
    print_string(":");
    print_int(line);
    print_string(": ");
    print_string(msg);
    print_string("\n");

    /* Halt the system - in real kernel, this would cli and hlt */
    while (1) {
        /* Infinite loop */
    }
}
