#include "kernel.h"

/*
 * PS/2 Keyboard driver
 * Handles keyboard interrupts and scancode translation
 */

// Keyboard I/O ports
#define KEYBOARD_DATA       0x60
#define KEYBOARD_STATUS     0x64
#define KEYBOARD_COMMAND    0x64

// Keyboard status register bits
#define KEYBOARD_STATUS_OUTPUT_FULL   0x01
#define KEYBOARD_STATUS_INPUT_FULL    0x02
#define KEYBOARD_STATUS_SYSTEM_FLAG   0x04
#define KEYBOARD_STATUS_COMMAND_DATA  0x08
#define KEYBOARD_STATUS_INHIBIT       0x10
#define KEYBOARD_STATUS_TRANSMIT_TIMEOUT 0x20
#define KEYBOARD_STATUS_RECEIVE_TIMEOUT 0x40
#define KEYBOARD_STATUS_PARITY_ERROR     0x80

// Keyboard commands
#define KEYBOARD_CMD_LED     0xED
#define KEYBOARD_CMD_ECHO    0xEE
#define KEYBOARD_CMD_SET_SCANCODE_SET 0xF0
#define KEYBOARD_CMD_ID      0xF2
#define KEYBOARD_CMD_SET_RATE 0xF3
#define KEYBOARD_CMD_ENABLE  0xF4
#define KEYBOARD_CMD_RESET   0xFF

// Scancode set 1 translation table (US layout)
static const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Modifier key states
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;

// Last input character for CLI
char last_key = 0;

// Forward declarations for static functions
static void keyboard_handle_keypress(uint8_t scancode);
static void keyboard_handle_keyrelease(uint8_t scancode);
static void keyboard_send_command(uint8_t command);
static void keyboard_send_data(uint8_t data);
static uint8_t keyboard_read_data(void);

// Initialize the keyboard
void keyboard_init(void)
{
    KINFO("Initializing PS/2 keyboard...");

    // Enable keyboard (send enable scanning command)
    keyboard_send_command(KEYBOARD_CMD_ENABLE);

    KINFO("Keyboard initialized");
}

// Handle keyboard interrupt
void keyboard_handler(void)
{
    // Read scancode from keyboard data port
    uint8_t scancode = inb(KEYBOARD_DATA);

    // Handle key press/release
    if (scancode < 128) {
        // Key press
        keyboard_handle_keypress(scancode);
    } else {
        // Key release (bit 7 set)
        scancode &= 0x7F; // Clear release bit
        keyboard_handle_keyrelease(scancode);
    }
}

// Handle key press events
static void keyboard_handle_keypress(uint8_t scancode)
{
    char ascii = 0;

    // Handle modifier keys
    switch (scancode) {
        case 0x2A: // Left Shift
        case 0x36: // Right Shift
            shift_pressed = true;
            return;
        case 0x1D: // Left Ctrl
            ctrl_pressed = true;
            return;
        case 0x38: // Left Alt
            alt_pressed = true;
            return;
        case 0x3A: // Caps Lock
            // Toggle caps lock (simplified - just ignore for now)
            return;
    }

    // Get ASCII character
    if (scancode < 128) {
        ascii = scancode_to_ascii[scancode];

        // Apply shift modifier (very basic)
        if (shift_pressed) {
            if (ascii >= 'a' && ascii <= 'z') {
                ascii = ascii - 'a' + 'A';
            } else {
                // Handle shifted symbols (very basic)
                switch (ascii) {
                    case '1': ascii = '!'; break;
                    case '2': ascii = '@'; break;
                    case '3': ascii = '#'; break;
                    case '4': ascii = '$'; break;
                    case '5': ascii = '%'; break;
                    case '6': ascii = '^'; break;
                    case '7': ascii = '&'; break;
                    case '8': ascii = '*'; break;
                    case '9': ascii = '('; break;
                    case '0': ascii = ')'; break;
                    case '-': ascii = '_'; break;
                    case '=': ascii = '+'; break;
                    case '[': ascii = '{'; break;
                    case ']': ascii = '}'; break;
                    case '\\': ascii = '|'; break;
                    case ';': ascii = ':'; break;
                    case '\'': ascii = '"'; break;
                    case ',': ascii = '<'; break;
                    case '.': ascii = '>'; break;
                    case '/': ascii = '?'; break;
                }
            }
        }

        // Set last key for CLI
        if (ascii) {
            last_key = ascii;
        }
    }
}

// Handle key release events
static void keyboard_handle_keyrelease(uint8_t scancode)
{
    // Handle modifier key releases
    switch (scancode) {
        case 0x2A: // Left Shift
        case 0x36: // Right Shift
            shift_pressed = false;
            break;
        case 0x1D: // Left Ctrl
            ctrl_pressed = false;
            break;
        case 0x38: // Left Alt
            alt_pressed = false;
            break;
    }
}

// Send command to keyboard
static void keyboard_send_command(uint8_t command)
{
    // Wait for keyboard to be ready
    while (inb(KEYBOARD_STATUS) & KEYBOARD_STATUS_INPUT_FULL) {
        // Wait
    }

    // Send command
    outb(KEYBOARD_COMMAND, command);

    // Wait a bit
    io_wait();
}

// Send data to keyboard
static void keyboard_send_data(uint8_t data)
{
    // Wait for keyboard to be ready
    while (inb(KEYBOARD_STATUS) & KEYBOARD_STATUS_INPUT_FULL) {
        // Wait
    }

    // Send data
    outb(KEYBOARD_DATA, data);

    // Wait a bit
    io_wait();
}

// Read data from keyboard (with timeout)
static uint8_t keyboard_read_data(void)
{
    // Wait for data to be available (with timeout)
    int timeout = 100000;
    while (!(inb(KEYBOARD_STATUS) & KEYBOARD_STATUS_OUTPUT_FULL) && timeout > 0) {
        timeout--;
    }

    if (timeout == 0) {
        return 0; // Timeout
    }

    return inb(KEYBOARD_DATA);
}
