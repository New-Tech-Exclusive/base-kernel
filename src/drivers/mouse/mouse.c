/*
 * PS/2 Mouse Driver
 */

#include "kernel.h"
#include "io.h"
#include "drivers/mouse.h"

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE_PACKET 0xF4

static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];
static mouse_state_t current_state;

// Screen dimensions (extern from framebuffer)
extern uint32_t current_width;
extern uint32_t current_height;

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 1) == 1) return;
        }
        return;
    } else {
        while (timeout--) {
            if ((inb(MOUSE_PORT_STATUS) & 2) == 0) return;
        }
        return;
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_PORT_DATA);
}

void mouse_init(void) {
    uint8_t status;

    KINFO("Initializing PS/2 Mouse...");

    // Enable auxiliary device
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0xA8);

    // Enable interrupts
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x20);
    mouse_wait(0);
    status = (inb(MOUSE_PORT_DATA) | 2);
    mouse_wait(1);
    outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT_DATA, status);

    // Set defaults
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    mouse_read(); // ACK

    // Enable packet streaming
    mouse_write(MOUSE_CMD_ENABLE_PACKET);
    mouse_read(); // ACK

    // Initialize state
    current_state.x = 1024 / 2;
    current_state.y = 768 / 2;
    current_state.left_button = 0;
    current_state.right_button = 0;
    current_state.middle_button = 0;
    
    // Register interrupt handler (IRQ 12) - simplified, assuming IDT is set up to call mouse_handler
    // interrupt_register(12, mouse_handler);
    
    KINFO("Mouse initialized.");
}

void mouse_handler(void) {
    uint8_t status = inb(MOUSE_PORT_STATUS);
    if (!(status & 1)) return;
    if (!(status & 0x20)) return; // Not mouse

    uint8_t in = inb(MOUSE_PORT_DATA);

    switch (mouse_cycle) {
        case 0:
            if ((in & 0x08) == 0x08) { // Sync bit verification
                mouse_byte[0] = in;
                mouse_cycle++;
            }
            break;
        case 1:
            mouse_byte[1] = in;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = in;
            mouse_cycle = 0;

            // Decode packet
            int8_t x_rel = mouse_byte[1];
            int8_t y_rel = mouse_byte[2];
            
            // Update state
            current_state.x += x_rel;
            current_state.y -= y_rel; // Y is inverted in PS/2

            // Clamp to screen
            if (current_state.x < 0) current_state.x = 0;
            if (current_state.y < 0) current_state.y = 0;
            // Note: current_width/height might not be visible here, hardcoding for safety if needed
            // but we declared them extern
            
            // Buttons
            current_state.left_button = (mouse_byte[0] & 1);
            current_state.right_button = (mouse_byte[0] & 2);
            current_state.middle_button = (mouse_byte[0] & 4);
            
            // Post event to event system (if we had one fully working)
            // For now, the desktop task will poll this state
            break;
    }
}

void mouse_get_state(mouse_state_t* state) {
    *state = current_state;
}
