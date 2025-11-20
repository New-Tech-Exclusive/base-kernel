/*
 * Desktop Environment
 * The main graphical user interface
 */

#include "kernel.h"
#include "drivers/mouse.h"

// Cursor bitmap (16x16 simple arrow)
static uint32_t cursor_bitmap[16 * 16];

void init_cursor_bitmap(void) {
    for (int i = 0; i < 256; i++) cursor_bitmap[i] = 0; // Transparent
    
    // Draw arrow
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 12; x++) {
            if (x == 0 || x == y || y == 15 || x == 11) {
                 // Border
            }
            if (x < y && x < 10) {
                cursor_bitmap[y * 16 + x] = 0xFFFFFFFF; // White
            }
            if (x == y || x == 0) {
                cursor_bitmap[y * 16 + x] = 0xFF000000; // Black border
            }
        }
    }
}

void draw_cursor(int x, int y) {
    // Simple software cursor
    // Save background? For now, just draw over (flicker possible without double buffering)
    // In a real compositor, cursor is a hardware sprite or composited last
    
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            uint32_t color = cursor_bitmap[cy * 16 + cx];
            if (color != 0) {
                framebuffer_put_pixel(x + cx, y + cy, color);
            }
        }
    }
}

void desktop_task(void* arg) {
    KINFO("Starting Desktop Environment...");
    
    init_cursor_bitmap();
    
    // Screen dimensions
    uint32_t width = 1024;
    uint32_t height = 768;
    
    // Colors
    uint32_t bg_color = 0xFF204060; // Nice blue
    uint32_t taskbar_color = 0xFFC0C0C0; // Silver
    
    mouse_state_t mouse;
    mouse_state_t last_mouse = {0, 0, 0, 0, 0};
    
    // Main loop
    while (1) {
        // 1. Draw Background (Wallpaper)
        // Optimization: Only redraw if needed (damaged regions)
        // For simplicity, we redraw everything (slow but correct)
        framebuffer_fill_rect(0, 0, width, height - 40, bg_color);
        
        // 2. Draw Taskbar
        framebuffer_fill_rect(0, height - 40, width, 40, taskbar_color);
        
        // Start Button
        framebuffer_fill_rect(5, height - 35, 80, 30, 0xFF808080);
        framebuffer_draw_text(15, height - 25, "START", 0xFFFFFFFF);
        
        // Clock (simulated)
        uint64_t ticks = sys_get_ticks();
        // char time_str[32];
        // sprintf(time_str, "%d", ticks);
        // framebuffer_draw_text(width - 100, height - 25, time_str, 0xFF000000);
        
        // 3. Composite Windows
        // (Iterate through windows and draw them)
        // window_composite_all(); // Not exposed yet, need to add to framebuffer.c
        
        // 4. Draw Cursor
        mouse_get_state(&mouse);
        draw_cursor(mouse.x, mouse.y);
        
        // 5. Handle Input
        if (mouse.left_button && !last_mouse.left_button) {
            // Click event
            if (mouse.y > height - 40 && mouse.x < 90) {
                // Start button clicked
                KINFO("Start button clicked!");
                // Launch a terminal window?
                // sys_window_create(...)
            }
        }
        
        last_mouse = mouse;
        
        // Yield
        scheduler_yield();
        
        // Delay to limit FPS
        for (volatile int i = 0; i < 1000000; i++);
    }
}

void desktop_init(void) {
    scheduler_create_task(desktop_task, NULL, 16384, 10, "Desktop");
}
