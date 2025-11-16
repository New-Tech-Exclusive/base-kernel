#include "kernel.h"

// Custom keyboard poll function
unsigned char keyboard_poll(void) {
    unsigned char scancode = 0;
    // Poll keyboard controller
    __asm__ volatile ("inb $0x60, %0" : "=a"(scancode));
    return scancode;
}

char keyboard_getchar(void) {
    static unsigned char scancodes_ascii[] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    };

    static int shift_pressed = 0;
    static int caps_lock = 0;
    static int key_down = 0;

    while (1) {
        unsigned char scancode = keyboard_poll();
        if (scancode == 0x2A || scancode == 0x36) {
            // Left/Right Shift press
            shift_pressed = 1;
            continue;
        } else if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) {
            // Left/Right Shift release
            shift_pressed = 0;
            continue;
        } else if (scancode == 0x3A) {
            // Caps Lock
            caps_lock = !caps_lock;
            continue;
        } else if (scancode & 0x80) {
            // Key release
            key_down = 0;
            continue;
        } else if (scancode < 128 && !key_down) {
            // Key press and not already down
            key_down = 1;
            if (scancode < sizeof(scancodes_ascii)) {
                char c = scancodes_ascii[scancode];
                if (c >= 'a' && c <= 'z') {
                    if (shift_pressed || (caps_lock && !shift_pressed)) {
                        c = c - 'a' + 'A';  // Uppercase
                    }
                } else if (shift_pressed) {
                    // Other shift mappings
                    if (c == '1') c = '!';
                    else if (c == '2') c = '@';
                    else if (c == '3') c = '#';
                    else if (c == '4') c = '$';
                    else if (c == '5') c = '%';
                    else if (c == '6') c = '^';
                    else if (c == '7') c = '&';
                    else if (c == '8') c = '*';
                    else if (c == '9') c = '(';
                    else if (c == '0') c = ')';
                    else if (c == '-') c = '_';
                    else if (c == '=') c = '+';
                    else if (c == '[') c = '{';
                    else if (c == ']') c = '}';
                    else if (c == '\\') c = '|';
                    else if (c == ';') c = ':';
                    else if (c == '\'') c = '"';
                    else if (c == ',') c = '<';
                    else if (c == '.') c = '>';
                    else if (c == '/') c = '?';
                }
                return c;
            }
        } else {
            // Other cases or default
            (void)0;
        }
        // Faster polling
        // for (volatile int i = 0; i < 10; i++);
    }
}
