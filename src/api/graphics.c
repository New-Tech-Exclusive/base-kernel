/*
 * Enhanced Graphics API Implementation
 * Provides high-level graphics operations with context-based drawing
 */

#include "kernel.h"

// ============================================================================
// WINDOW MANAGEMENT
// ============================================================================

window_id_t window_create(const window_config_t* config) {
    if (!config) return 0;

    // Connect to display server
    int64_t status = sys_connect_display_server();
    if (status < 0) {
        KERROR("Failed to connect to display server: %ld", status);
        return 0;
    }

    // Create window through display protocol
    int window_id = sys_display_create_window(config->x, config->y,
                                            config->width, config->height,
                                            config->title);

    return (window_id_t)window_id;
}

void window_destroy(window_id_t window) {
    if (window == 0) return;

    sys_display_destroy_window((int)window);
}

void window_show(window_id_t window) {
    // Windows are shown by default in our implementation
    (void)window; // Suppress unused parameter warning
}

void window_hide(window_id_t window) {
    // Basic implementation - in real system would hide from compositor
    (void)window; // Suppress unused parameter warning
}

void window_move(window_id_t window, int x, int y) {
    // Our display server doesn't support move yet
    (void)window; (void)x; (void)y;
}

void window_resize(window_id_t window, int width, int height) {
    // Our display server doesn't support resize yet
    (void)window; (void)width; (void)height;
}

bool window_is_visible(window_id_t window) {
    // Assume visible - would check compositor state
    (void)window;
    return true;
}

// ============================================================================
// GRAPHICS CONTEXT MANAGEMENT
// ============================================================================

void graphics_begin_frame(window_id_t window, graphics_context_t* ctx) {
    if (!ctx) return;

    // Initialize context with default values
    ctx->x = 0;
    ctx->y = 0;
    ctx->bg_color = 0xFF000000; // Black
    ctx->fg_color = 0xFFFFFFFF; // White

    // Get window dimensions - simplified
    ctx->width = 800;  // Default assumptions
    ctx->height = 600;

    // Set default clipping to full window
    ctx->clip_x = 0;
    ctx->clip_y = 0;
    ctx->clip_w = ctx->width;
    ctx->clip_h = ctx->height;

    (void)window; // Use window if needed for real implementation
}

void graphics_end_frame(window_id_t window) {
    if (window == 0) return;

    // Tell display server to composite/update this window
    sys_display_composite_window((int)window);
}

// ============================================================================
// CONTEXT-BASED DRAWING PRIMITIVES
// ============================================================================

static bool clip_rect(const graphics_context_t* ctx, int* x, int* y, int* w, int* h) {
    if (!ctx || !x || !y || !w || !h) return false;

    // Clip to context bounds first
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > ctx->width) *w = ctx->width - *x;
    if (*y + *h > ctx->height) *h = ctx->height - *y;

    // Clip to clipping rectangle
    if (*x < ctx->clip_x) { *w -= (ctx->clip_x - *x); *x = ctx->clip_x; }
    if (*y < ctx->clip_y) { *h -= (ctx->clip_y - *y); *y = ctx->clip_y; }
    if (*x + *w > ctx->clip_x + ctx->clip_w) *w = ctx->clip_x + ctx->clip_w - *x;
    if (*y + *h > ctx->clip_y + ctx->clip_h) *h = ctx->clip_y + ctx->clip_h - *y;

    return (*w > 0 && *h > 0);
}

void graphics_clear(const graphics_context_t* ctx) {
    if (!ctx) return;

    int x = 0, y = 0, w = ctx->width, h = ctx->height;
    if (clip_rect(ctx, &x, &y, &w, &h)) {
        // Use syscall to draw the rectangle
        sys_draw_rect(0, x, y, w, h, ctx->bg_color); // Window 0 = current window
    }
}

void graphics_draw_rect(const graphics_context_t* ctx, int x, int y, int w, int h, uint32_t color) {
    if (!ctx || w <= 0 || h <= 0) return;

    if (clip_rect(ctx, &x, &y, &w, &h)) {
        sys_draw_rect(0, x, y, w, h, color);
    }
}

void graphics_draw_circle(const graphics_context_t* ctx, int cx, int cy, int radius, uint32_t color) {
    if (!ctx || radius <= 0) return;

    // Basic circle drawing using Bresenham's algorithm
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    // Draw circle outline
    while (x <= y) {
        // Draw 8 octants
        if (cx + x >= ctx->clip_x && cx + x < ctx->clip_x + ctx->clip_w &&
            cy + y >= ctx->clip_y && cy + y < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx + x, cy + y, 1, 1, color);
        }
        if (cx + x >= ctx->clip_x && cx + x < ctx->clip_x + ctx->clip_w &&
            cy - y >= ctx->clip_y && cy - y < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx + x, cy - y, 1, 1, color);
        }
        if (cx - x >= ctx->clip_x && cx - x < ctx->clip_x + ctx->clip_w &&
            cy + y >= ctx->clip_y && cy + y < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx - x, cy + y, 1, 1, color);
        }
        if (cx - x >= ctx->clip_x && cx - x < ctx->clip_x + ctx->clip_w &&
            cy - y >= ctx->clip_y && cy - y < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx - x, cy - y, 1, 1, color);
        }
        if (cx + y >= ctx->clip_x && cx + y < ctx->clip_x + ctx->clip_w &&
            cy + x >= ctx->clip_y && cy + x < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx + y, cy + x, 1, 1, color);
        }
        if (cx + y >= ctx->clip_x && cx + y < ctx->clip_x + ctx->clip_w &&
            cy - x >= ctx->clip_y && cy - x < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx + y, cy - x, 1, 1, color);
        }
        if (cx - y >= ctx->clip_x && cx - y < ctx->clip_x + ctx->clip_w &&
            cy + x >= ctx->clip_y && cy + x < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx - y, cy + x, 1, 1, color);
        }
        if (cx - y >= ctx->clip_x && cx - y < ctx->clip_x + ctx->clip_w &&
            cy - x >= ctx->clip_y && cy - x < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, cx - y, cy - x, 1, 1, color);
        }

        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void graphics_draw_line(const graphics_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color) {
    if (!ctx) return;

    // Bresenham's line algorithm
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    int x = x1;
    int y = y1;

    while (1) {
        // Draw pixel if within bounds
        if (x >= ctx->clip_x && x < ctx->clip_x + ctx->clip_w &&
            y >= ctx->clip_y && y < ctx->clip_y + ctx->clip_h) {
            sys_draw_rect(0, x, y, 1, 1, color);
        }

        // Check if we're done
        if (x == x2 && y == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void graphics_draw_text(const graphics_context_t* ctx, int x, int y, const char* text, uint32_t color) {
    if (!ctx || !text) return;

    // Very basic text rendering - would need real font in full implementation
    int current_x = x;
    const char* p = text;

    while (*p) {
        // Draw a simple block character (8x8 pixels)
        for (int dy = 0; dy < 8; dy++) {
            for (int dx = 0; dx < 6; dx++) {
                int draw_x = current_x + dx;
                int draw_y = y + dy;

                if (draw_x >= ctx->clip_x && draw_x < ctx->clip_x + ctx->clip_w &&
                    draw_y >= ctx->clip_y && draw_y < ctx->clip_y + ctx->clip_h) {
                    // Simple pattern for each character
                    uint32_t pixel_color = (dx < 4 && dy < 6) ? color : ctx->bg_color;
                    sys_draw_rect(0, draw_x, draw_y, 1, 1, pixel_color);
                }
            }
        }

        current_x += 7; // 6px width + 1px spacing
        p++;
    }
}

// ============================================================================
// ADVANCED GRAPHICS FUNCTIONS
// ============================================================================

void graphics_draw_gradient_rect(const graphics_context_t* ctx, int x, int y, int w, int h,
                                uint32_t color1, uint32_t color2, bool vertical) {
    if (!ctx || w <= 0 || h <= 0) return;

    int clip_x = x, clip_y = y, clip_w = w, clip_h = h;
    if (!clip_rect(ctx, &clip_x, &clip_y, &clip_w, &clip_h)) return;

    // Simple gradient implementation
    for (int dy = 0; dy < clip_h; dy++) {
        for (int dx = 0; dx < clip_w; dx++) {
            float factor = vertical ? (float)dy / h : (float)dx / w;

            // Simple color interpolation
            uint8_t r1 = (color1 >> 16) & 0xFF;
            uint8_t g1 = (color1 >> 8) & 0xFF;
            uint8_t b1 = color1 & 0xFF;
            uint8_t r2 = (color2 >> 16) & 0xFF;
            uint8_t g2 = (color2 >> 8) & 0xFF;
            uint8_t b2 = color2 & 0xFF;

            uint8_t r = r1 + (r2 - r1) * factor;
            uint8_t g = g1 + (g2 - g1) * factor;
            uint8_t b = b1 + (b2 - b1) * factor;

            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;

            sys_draw_rect(0, clip_x + dx, clip_y + dy, 1, 1, color);
        }
    }
}

void graphics_draw_border(const graphics_context_t* ctx, int x, int y, int w, int h,
                         uint32_t color, int thickness) {
    if (!ctx || thickness <= 0) return;

    // Draw border as 4 rectangles
    // Top
    graphics_draw_rect(ctx, x, y, w, thickness, color);
    // Bottom
    graphics_draw_rect(ctx, x, y + h - thickness, w, thickness, color);
    // Left
    graphics_draw_rect(ctx, x, y + thickness, thickness, h - 2 * thickness, color);
    // Right
    graphics_draw_rect(ctx, x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}
