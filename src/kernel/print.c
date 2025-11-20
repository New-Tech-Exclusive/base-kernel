#include "kernel.h"
#include <stdarg.h>

/*
 * Basic printing functions for kernel debugging
 */

static void print_char(char c)
{
    serial_write(c);
}

static void print_string(const char* str)
{
    while (*str) {
        print_char(*str++);
    }
}

// Helper for number conversion
static int itoa(long long value, char* str, int base, int width, int pad_zero)
{
    char buffer[64];
    char* ptr = buffer;
    char* low;
    int count = 0;
    unsigned long long uvalue;
    int negative = 0;

    if (base == 10 && value < 0) {
        uvalue = -value;
        negative = 1;
    } else {
        uvalue = (unsigned long long)value;
    }

    // Conversion
    do {
        int digit = uvalue % base;
        *ptr++ = (digit < 10) ? (digit + '0') : (digit - 10 + 'a');
        uvalue /= base;
        count++;
    } while (uvalue);

    // Padding
    while (count < width) {
        *ptr++ = pad_zero ? '0' : ' ';
        count++;
    }

    if (negative) {
        *ptr++ = '-';
        count++;
    }

    // Reverse
    low = buffer;
    ptr--;
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }

    // Copy to output
    int i = 0;
    while (i < count) {
        *str++ = buffer[i++];
    }
    *str = '\0';
    return count;
}

int vsnprintf(char* str, size_t size, const char* format, va_list ap)
{
    size_t written = 0;
    char* out = str;
    const char* fmt = format;

    while (*fmt && written < size - 1) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            written++;
            continue;
        }

        fmt++; // Skip '%'
        
        // Parse width/padding
        int width = 0;
        int pad_zero = 0;
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        // Parse length modifier
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        }

        switch (*fmt) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s && written < size - 1) {
                    *out++ = *s++;
                    written++;
                }
                break;
            }
            case 'd':
            case 'i': {
                long long val;
                if (is_long == 2) val = va_arg(ap, long long);
                else if (is_long == 1) val = va_arg(ap, long);
                else val = va_arg(ap, int);
                
                char buf[32];
                int len = itoa(val, buf, 10, width, pad_zero);
                for (int i = 0; i < len && written < size - 1; i++) {
                    *out++ = buf[i];
                    written++;
                }
                break;
            }
            case 'u': {
                unsigned long long val;
                if (is_long == 2) val = va_arg(ap, unsigned long long);
                else if (is_long == 1) val = va_arg(ap, unsigned long);
                else val = va_arg(ap, unsigned int);
                
                char buf[32];
                int len = itoa(val, buf, 10, width, pad_zero);
                for (int i = 0; i < len && written < size - 1; i++) {
                    *out++ = buf[i];
                    written++;
                }
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                unsigned long long val;
                if (*fmt == 'p') {
                    val = (unsigned long long)va_arg(ap, void*);
                    *out++ = '0'; written++;
                    *out++ = 'x'; written++;
                    width = 0; // Reset width for pointer prefix
                } else if (is_long == 2) {
                    val = va_arg(ap, unsigned long long);
                } else if (is_long == 1) {
                    val = va_arg(ap, unsigned long);
                } else {
                    val = va_arg(ap, unsigned int);
                }
                
                char buf[32];
                int len = itoa(val, buf, 16, width, pad_zero);
                for (int i = 0; i < len && written < size - 1; i++) {
                    *out++ = buf[i];
                    written++;
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                *out++ = c;
                written++;
                break;
            }
            case '%': {
                *out++ = '%';
                written++;
                break;
            }
        }
        fmt++;
    }
    *out = '\0';
    return written;
}

int sprintf(char* str, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, 65536, format, args); // Unsafe max size, but standard sprintf is unsafe
    va_end(args);
    return ret;
}

void kprintf(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    print_string(buf);
}

/* Debug output functions */
void kernel_debug(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    print_string("[DEBUG] ");
    print_string(buf);
    print_string("\n");
}

void kernel_info(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    print_string("[INFO]  ");
    print_string(buf);
    print_string("\n");
}

void kernel_warn(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    print_string("[WARN]  ");
    print_string(buf);
    print_string("\n");
}

void kernel_error(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    print_string("[ERROR] ");
    print_string(buf);
    print_string("\n");
}

/* Panic function */
__NORETURN void kernel_panic(const char* file, int line, const char* msg)
{
    kprintf("KERNEL PANIC at %s:%d: %s\n", file, line, msg);

    /* Halt the system - in real kernel, this would cli and hlt */
    while (1) {
        /* Infinite loop */
    }
}
