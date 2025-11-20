/*
 * Framebuffer Graphics Driver
 * VESA-compatible linear framebuffer for GUI display
 */

#include "kernel.h"

// Framebuffer information (returned by VESA BIOS)
typedef struct {
    uint32_t address;      // Linear framebuffer base address
    uint32_t width;        // Screen width in pixels
    uint32_t height;       // Screen height in pixels
    uint32_t pitch;        // Bytes per line
    uint8_t bpp;          // Bits per pixel
    uint8_t red_mask;     // Red color mask
    uint8_t green_mask;   // Green color mask
    uint8_t blue_mask;    // Blue color mask
} __PACKED framebuffer_info_t;

// Global framebuffer state
static framebuffer_info_t* fb_info = NULL;
static volatile uint8_t* fb_buffer = NULL;
static uint32_t current_width = 1024;
static uint32_t current_height = 768;
static uint8_t current_bpp = 32;

// Color definitions for 32-bit RGBA
#define RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define RGB(r,g,b)    RGBA(r,g,b,0xFF)

// Basic colors
#define COLOR_BLACK     RGB(0x00, 0x00, 0x00)
#define COLOR_WHITE     RGB(0xFF, 0xFF, 0xFF)
#define COLOR_RED       RGB(0xFF, 0x00, 0x00)
#define COLOR_GREEN     RGB(0x00, 0xFF, 0x00)
#define COLOR_BLUE      RGB(0x00, 0x00, 0xFF)
#define COLOR_YELLOW    RGB(0xFF, 0xFF, 0x00)
#define COLOR_MAGENTA   RGB(0xFF, 0x00, 0xFF)
#define COLOR_CYAN      RGB(0x00, 0xFF, 0xFF)
#define COLOR_GRAY      RGB(0x80, 0x80, 0x80)

// Display protocol structures
#define DISPLAY_MAGIC 0x44495350  // "DISP"
#define MAX_WINDOWS 64

typedef struct {
    uint32_t magic;       // DISPLAY_MAGIC
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    volatile uint8_t* buffer;
} display_info_t;

// Window management
typedef struct {
    int id;
    int x, y;           // Position
    int width, height;  // Size
    int visible;        // Visibility flag
    int z_index;        // Z-order
    volatile uint8_t* buffer; // Back buffer
    pid_t owner_pid;    // Owning process
} window_t;

static window_t windows[MAX_WINDOWS];
static int next_window_id = 1;
static display_info_t display_info;

// ============================================================================
// FRAMEBUFFER INITIALIZATION
// ============================================================================

// Forward declarations
void framebuffer_clear(uint32_t color);
void framebuffer_draw_test_pattern(void);

int framebuffer_init(void)
{
    KINFO("=====================================");
    KINFO("Initializing Framebuffer Graphics");
    KINFO("");

    // Allocate display info structure
    display_info.magic = DISPLAY_MAGIC;
    display_info.width = current_width;
    display_info.height = current_height;
    display_info.bpp = current_bpp;
    display_info.pitch = current_width * (current_bpp / 8);

    // Allocate virtual memory for framebuffer (simulate mapping)
    // In real hardware, this would map VESA linear framebuffer
    size_t fb_size = current_width * current_height * (current_bpp / 8);

    // For demo, we'll use a smaller buffer that fits in RAM
    // Real implementation would map hardware framebuffer
    fb_buffer = (volatile uint8_t*)kmalloc(1024 * 768 * 4); // 3MB buffer for demo
    if (!fb_buffer) {
        KERROR("Failed to allocate framebuffer memory");
        return -1;
    }

    display_info.buffer = fb_buffer;

    KINFO("📐 Framebuffer Graphics Initialized:");
    KINFO("  ├─ Resolution: %ux%u", current_width, current_height);
    KINFO("  ├─ Color depth: %u bits per pixel", current_bpp);
    KINFO("  ├─ Framebuffer size: %u KB", fb_size / 1024);
    KINFO("  ├─ Pitch: %u bytes per line", display_info.pitch);
    KINFO("  └─ Address: 0x%lx", (uintptr_t)fb_buffer);

    // Clear screen to black
    framebuffer_clear(COLOR_BLACK);

    // Draw test pattern
    framebuffer_draw_test_pattern();

    KINFO("✅ Framebuffer ready for graphics operations!");
    KINFO("==========================================");

    return 0;
}

// ============================================================================
// BASIC DRAWING OPERATIONS
// ============================================================================

// Clear entire screen to color
void framebuffer_clear(uint32_t color)
{
    if (!fb_buffer) return;

    uint32_t* buffer = (uint32_t*)fb_buffer;
    for (uint32_t i = 0; i < current_width * current_height; i++) {
        buffer[i] = color;
    }
}

// Plot a single pixel
void framebuffer_put_pixel(int x, int y, uint32_t color)
{
    if (!fb_buffer || x < 0 || x >= current_width || y < 0 || y >= current_height) {
        return;
    }

    volatile uint32_t* buffer = (volatile uint32_t*)fb_buffer;
    buffer[y * current_width + x] = color;
}

// Get pixel color
uint32_t framebuffer_get_pixel(int x, int y)
{
    if (!fb_buffer || x < 0 || x >= current_width || y < 0 || y >= current_height) {
        return 0;
    }

    volatile uint32_t* buffer = (volatile uint32_t*)fb_buffer;
    return buffer[y * current_width + x];
}

// ============================================================================
// GRAPHICS PRIMITIVES
// ============================================================================

// Draw a filled rectangle
void framebuffer_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    if (!fb_buffer) return;

    // Clip to screen bounds
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > current_width) width = current_width - x;
    if (y + height > current_height) height = current_height - y;

    if (width <= 0 || height <= 0) return;

    volatile uint32_t* buffer = (volatile uint32_t*)fb_buffer;

    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            buffer[py * current_width + px] = color;
        }
    }
}

// Draw rectangle outline
void framebuffer_draw_rect(int x, int y, int width, int height, uint32_t color)
{
    // Top and bottom
    framebuffer_fill_rect(x, y, width, 1, color);
    framebuffer_fill_rect(x, y + height - 1, width, 1, color);

    // Left and right (excluding already drawn corners)
    framebuffer_fill_rect(x, y + 1, 1, height - 2, color);
    framebuffer_fill_rect(x + width - 1, y + 1, 1, height - 2, color);
}

// Draw a line (Bresenham's algorithm)
void framebuffer_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int dx_abs = dx < 0 ? -dx : dx;
    int dy_abs = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = dx_abs - dy_abs;
    int err2;

    while (1) {
        framebuffer_put_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        err2 = 2 * err;

        if (err2 > -dy_abs) {
            err -= dy_abs;
            x0 += sx;
        }

        if (err2 < dx_abs) {
            err += dx_abs;
            y0 += sy;
        }
    }
}

// Draw a circle (midpoint algorithm)
void framebuffer_draw_circle(int center_x, int center_y, int radius, uint32_t color)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        // Draw all 8 octants
        framebuffer_put_pixel(center_x + x, center_y + y, color);
        framebuffer_put_pixel(center_x + y, center_y + x, color);
        framebuffer_put_pixel(center_x - y, center_y + x, color);
        framebuffer_put_pixel(center_x - x, center_y + y, color);
        framebuffer_put_pixel(center_x - x, center_y - y, color);
        framebuffer_put_pixel(center_x - y, center_y - x, color);
        framebuffer_put_pixel(center_x + y, center_y - x, color);
        framebuffer_put_pixel(center_x + x, center_y - y, color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }

        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

// ============================================================================
// TEST PATTERN FOR DEMO
// ============================================================================

void framebuffer_draw_test_pattern(void)
{
    // Draw color bars at top
    int bar_width = current_width / 8;
    framebuffer_fill_rect(0, 0, bar_width, 50, COLOR_RED);
    framebuffer_fill_rect(bar_width, 0, bar_width, 50, COLOR_GREEN);
    framebuffer_fill_rect(bar_width * 2, 0, bar_width, 50, COLOR_BLUE);
    framebuffer_fill_rect(bar_width * 3, 0, bar_width, 50, COLOR_YELLOW);
    framebuffer_fill_rect(bar_width * 4, 0, bar_width, 50, COLOR_MAGENTA);
    framebuffer_fill_rect(bar_width * 5, 0, bar_width, 50, COLOR_CYAN);
    framebuffer_fill_rect(bar_width * 6, 0, bar_width, 50, COLOR_WHITE);
    framebuffer_fill_rect(bar_width * 7, 0, bar_width, 50, COLOR_GRAY);

    // Draw some shapes
    framebuffer_draw_rect(100, 100, 200, 150, COLOR_WHITE);
    framebuffer_draw_circle(current_width - 150, 150, 80, COLOR_BLUE);
    framebuffer_draw_line(50, 300, current_width - 50, 350, COLOR_GREEN);

    // Draw checkerboard pattern
    for (int y = current_height - 200; y < current_height - 100; y += 20) {
        for (int x = 50; x < 250; x += 20) {
            uint32_t color = ((x / 20) + (y / 20)) % 2 ? COLOR_WHITE : COLOR_BLACK;
            framebuffer_fill_rect(x, y, 20, 20, color);
        }
    }
}

// ============================================================================
// WINDOW MANAGEMENT (FOR DISPLAY SERVER)
// ============================================================================

// Create a new window
int window_create(int x, int y, int width, int height, pid_t owner_pid)
{
    // Find free window slot
    int window_id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == 0) {
            window_id = i;
            break;
        }
    }

    if (window_id == -1) {
        KERROR("No free window slots available");
        return -1;
    }

    // Initialize window
    windows[window_id].id = next_window_id++;
    windows[window_id].x = x;
    windows[window_id].y = y;
    windows[window_id].width = width;
    windows[window_id].height = height;
    windows[window_id].visible = 1;
    windows[window_id].z_index = 0;
    windows[window_id].owner_pid = owner_pid;

    // Allocate back buffer for window
    size_t back_buffer_size = width * height * 4; // 32-bit RGBA
    windows[window_id].buffer = (volatile uint8_t*)kmalloc(back_buffer_size);

    if (!windows[window_id].buffer) {
        windows[window_id].id = 0; // Mark as free
        KERROR("Failed to allocate window back buffer");
        return -1;
    }

    // Clear back buffer to transparent (alpha = 0)
    memset((void*)windows[window_id].buffer, 0, back_buffer_size);

    KDEBUG("Created window %d for process %d (%dx%d at %d,%d)",
           windows[window_id].id, owner_pid, width, height, x, y);

    return windows[window_id].id;
}

// Destroy a window
int window_destroy(int window_id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == window_id) {
            if (windows[i].buffer) {
                kfree((void*)windows[i].buffer);
            }
            memset(&windows[i], 0, sizeof(window_t));
            KDEBUG("Destroyed window %d", window_id);
            return 0;
        }
    }
    return -1; // Window not found
}

// Get window back buffer (for client drawing)
volatile uint8_t* window_get_buffer(int window_id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == window_id) {
            return windows[i].buffer;
        }
    }
    return NULL;
}

// Mark window as ready for display (composite to main buffer)
void window_composite(int window_id)
{
    // Find window
    window_t* window = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == window_id) {
            window = &windows[i];
            break;
        }
    }

    if (!window || !window->visible) return;

    uint32_t* src = (uint32_t*)window->buffer;
    uint32_t* dst = (uint32_t*)fb_buffer;

    // Composite window to main framebuffer with alpha blending
    for (int wy = 0; wy < window->height; wy++) {
        int screen_y = window->y + wy;
        if (screen_y < 0 || screen_y >= current_height) continue;

        for (int wx = 0; wx < window->width; wx++) {
            int screen_x = window->x + wx;
            if (screen_x < 0 || screen_x >= current_width) continue;

            uint32_t pixel = src[wy * window->width + wx];

            // Simple alpha blending (if alpha > 128, fully opaque for demo)
            uint8_t alpha = (pixel >> 24) & 0xFF;
            if (alpha > 128) {
                dst[screen_y * current_width + screen_x] = pixel;
            }
        }
    }
}

// ============================================================================
// SYSCALL INTERFACE
// ============================================================================

// Get display information for clients
int64_t sys_get_display_info(uint32_t* width, uint32_t* height, uint32_t* bpp)
{
    if (!width || !height || !bpp) return -1;
    *width = display_info.width;
    *height = display_info.height;
    *bpp = display_info.bpp;
    return 0;
}

// Window management syscalls
int64_t sys_window_create(int x, int y, int width, int height)
{
    pid_t pid = scheduler_get_current_task_id();
    return window_create(x, y, width, height, pid);
}

int64_t sys_window_destroy(int window_id)
{
    return window_destroy(window_id);
}

int64_t sys_window_composite(int window_id)
{
    window_composite(window_id);
    return 0;
}

// Framebuffer access syscall
int64_t sys_framebuffer_access(void** framebuffer, uint32_t* width, uint32_t* height, uint32_t* bpp)
{
    if (!framebuffer || !width || !height || !bpp) return -1;
    
    // For now, return the main framebuffer
    // In a real implementation, this would return a window-specific buffer
    *framebuffer = (void*)fb_buffer;
    *width = current_width;
    *height = current_height;
    *bpp = current_bpp;
    return 0;
}

// Drawing primitives syscalls
int64_t sys_draw_rect(int window_id, int x, int y, int w, int h, uint32_t color)
{
    // For demo, draw directly to main framebuffer at position 300,300
    framebuffer_draw_rect(300, 300, w, h, color);
    return 0;
}

int64_t sys_draw_circle(int window_id, int center_x, int center_y, int radius, uint32_t color)
{
    // For demo, draw directly to main framebuffer at offset
    framebuffer_draw_circle(center_x + 200, center_y + 200, radius, color);
    return 0;
}

// Simple text rendering stub
void framebuffer_draw_text(int x, int y, const char* text, uint32_t color)
{
    // Stub implementation - in a real kernel, this would render actual text
    // For now, just draw a colored rectangle to indicate text position
    (void)text; // Unused for now
    framebuffer_fill_rect(x, y, 8, 12, color);
}

