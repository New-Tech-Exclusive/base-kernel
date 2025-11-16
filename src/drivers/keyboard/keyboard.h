#ifndef KEYBOARD_H
#define KEYBOARD_H

// Keyboard polling function
unsigned char keyboard_poll(void);

// Get a character with proper key handling (uppercase, shift, etc.)
char keyboard_getchar(void);

#endif
