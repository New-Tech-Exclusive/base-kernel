#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

// Mouse packet structure
typedef struct {
    uint8_t flags;
    int8_t x_offset;
    int8_t y_offset;
} mouse_packet_t;

// Mouse state
typedef struct {
    int32_t x;
    int32_t y;
    uint8_t left_button;
    uint8_t right_button;
    uint8_t middle_button;
} mouse_state_t;

// Functions
void mouse_init(void);
void mouse_handler(void);
void mouse_get_state(mouse_state_t* state);

#endif // MOUSE_H
