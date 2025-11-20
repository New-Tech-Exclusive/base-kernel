/*
 * Enhanced Kernel API - Developer-Friendly Interfaces
 * Provides high-level abstractions over low-level kernel functionality
 *
 * This header file provides the modern, developer-friendly API that makes
 * kernel development as comfortable as user-space programming.
 */

#ifndef API_H
#define API_H

#include "kernel.h"

// ============================================================================
// UTILITY MACROS AND HELPERS
// ============================================================================

// Error handling macros for cleaner code
#define TRY(expr) do { \
    int64_t result = (expr); \
    if (result < 0) return result; \
} while(0)

#define CHECK_NULL(ptr) do { \
    if (!(ptr)) return -EINVAL; \
} while(0)

#define VALIDATE_PARAMS(cond) do { \
    if (!(cond)) return -EINVAL; \
} while(0)

// Memory safety helpers
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

// ============================================================================
// ENHANCED MEMORY MANAGEMENT API
// ============================================================================

// Smart memory management with automatic cleanup
typedef struct memory_pool memory_pool_t;
typedef struct memory_tracker memory_tracker_t;

// File and directory types
typedef struct api_file file_t;
typedef struct dir dir_t;
typedef struct dir_entry dir_entry_t;

// High-level memory allocation with tracking
void* kmalloc_tracked(size_t size, const char* tag);
void* krealloc_tracked(void* ptr, size_t size, const char* tag);
void kfree_tracked(void* ptr);

// Memory pool for frequent allocations
memory_pool_t* memory_pool_create(size_t block_size, size_t initial_blocks);
void* memory_pool_alloc(memory_pool_t* pool);
void memory_pool_free(memory_pool_t* pool, void* ptr);
void memory_pool_destroy(memory_pool_t* pool);

// Memory statistics and debugging
// memory_stats_t is defined in kernel.h

void memory_get_stats(memory_stats_t* stats);
void memory_dump_leaks(void);

// Smart pointers (RAII-style cleanup)
typedef void (*cleanup_func_t)(void*);

typedef struct {
    void* ptr;
    cleanup_func_t cleanup;
} smart_ptr_t;

smart_ptr_t make_smart_ptr(void* ptr, cleanup_func_t cleanup);
void smart_ptr_cleanup(smart_ptr_t* sp);

// RAII-style allocations
#define AUTO_FREE __attribute__((cleanup(auto_kfree)))
#define AUTO_SMART __attribute__((cleanup(auto_smart_cleanup)))

void auto_kfree(void* ptr);
void auto_smart_cleanup(smart_ptr_t* sp);

// Error handling
typedef enum {
    KERNEL_SUCCESS = 0,
    KERNEL_ERROR_INVALID_ARGUMENT = -1,
    KERNEL_ERROR_NOT_FOUND = -2,
    KERNEL_ERROR_PERMISSION_DENIED = -3,
    KERNEL_ERROR_OUT_OF_MEMORY = -4,
    KERNEL_ERROR_IO_ERROR = -5,
    KERNEL_ERROR_NOT_IMPLEMENTED = -6,
    KERNEL_ERROR_TIMEOUT = -7,
    KERNEL_ERROR_BUSY = -8,
    KERNEL_ERROR_EXISTS = -9,
    KERNEL_ERROR_TOO_MANY = -10,
    KERNEL_ERROR_FILE_NOT_FOUND = -11,
    KERNEL_ERROR_DIRECTORY_NOT_EMPTY = -12,
    KERNEL_ERROR_FILE_TOO_BIG = -13,
    KERNEL_ERROR_NO_SPACE = -14
} kernel_error_t;

const char* kernel_error_string(kernel_error_t error);

// ============================================================================
// ENHANCED PROCESS MANAGEMENT API
// ============================================================================

// Process creation and management
typedef pid_t process_t;
typedef pid_t pgid_t;  // Process group ID
typedef void (*process_entry_t)(void* arg);

// Process state (maps to task_state_t)
typedef task_state_t process_state_t;

// Process information structure
typedef struct {
    pid_t pid;
    const char* name;
    task_state_t state;
    size_t stack_size;
    int priority;
    uint64_t creation_time;
    uint64_t cpu_time;
    size_t memory_used;
} process_info_t;

// Process attributes for easy configuration
typedef struct process_attr {
    const char* name;              // Process name for debugging
    size_t stack_size;             // Stack size (default: 8192)
    int priority;                  // Scheduling priority (0-255, default: 100)
    bool inherit_env;              // Inherit environment variables
    bool auto_cleanup;             // Automatically clean up on exit
    cleanup_func_t cleanup_func;    // Custom cleanup function
} process_attr_t;

// Default process attributes
#define PROCESS_ATTR_DEFAULT { \
    .name = "unnamed", \
    .stack_size = 8192, \
    .priority = 100, \
    .inherit_env = true, \
    .auto_cleanup = true, \
    .cleanup_func = NULL \
}

// High-level process API
process_t process_create(process_entry_t entry, void* arg, process_attr_t* attr);
process_t process_create_simple(process_entry_t entry, void* arg);
int process_wait(process_t pid);
int process_kill(process_t pid);
int process_get_info(process_t pid, process_info_t* info);

// Process groups and sessions
typedef int pgid_t;
typedef int sid_t;

pgid_t process_group_create(void);
int process_group_join(pgid_t pgid);
int process_group_kill(pgid_t pgid);

// Environment variables (if supported)
int process_set_env(const char* key, const char* value);
const char* process_get_env(const char* key);

// ============================================================================
// ENHANCED FILE SYSTEM API
// ============================================================================

// File operations with automatic resource management
// file_t is defined earlier as struct api_file
typedef int64_t file_mode_t;

// File open modes (more intuitive than raw flags)
typedef enum {
    FILE_MODE_READ = 1 << 0,
    FILE_MODE_WRITE = 1 << 1,
    FILE_MODE_EXECUTE = 1 << 2,
    FILE_MODE_CREATE = 1 << 3,
    FILE_MODE_TRUNCATE = 1 << 4,
    FILE_MODE_APPEND = 1 << 5,
} file_open_mode_t;

// File API with RAII
file_t* file_open(const char* path, file_open_mode_t mode);
void file_close(file_t* file);
size_t file_read(file_t* file, void* buffer, size_t size);
size_t file_write(file_t* file, const void* buffer, size_t size);
int64_t file_seek(file_t* file, int64_t offset, int whence);
size_t file_size(file_t* file);

// High-level file operations
char* file_read_all(const char* path);
int file_write_all(const char* path, const char* content);
int file_copy(const char* src, const char* dst);
int file_exists(const char* path);
bool file_is_directory(const char* path);

// Directory operations
typedef struct dir dir_t;

typedef struct dir_entry {
    const char* name;
    bool is_directory;
    size_t size;
    uint64_t modified_time;
} dir_entry_t;

dir_t* dir_open(const char* path);
dir_entry_t* dir_read(dir_t* dir);
void dir_close(dir_t* dir);
int dir_create(const char* path);
int dir_remove(const char* path);

// Path utilities
char* path_join(const char* base, const char* relative);
char* path_dirname(const char* path);
char* path_basename(const char* path);
bool path_is_absolute(const char* path);

// ============================================================================
// ENHANCED GRAPHICS API
// ============================================================================

// Window manager structures (exposed for event system)
typedef struct {
    int window_id;
    int x, y, width, height;
    int visible;
    pid_t owner_pid;
    char title[64];
} wm_window_t;

// Graphics context for high-level drawing
typedef struct {
    int x, y, width, height;
    uint32_t bg_color;
    uint32_t fg_color;
    int clip_x, clip_y, clip_w, clip_h;
} graphics_context_t;

// Window management API
typedef uint32_t window_id_t;

typedef struct window_config {
    const char* title;
    int x, y, width, height;
    uint32_t bg_color;
    bool resizable;
    bool closable;
    bool fullscreen;
} window_config_t;

// High-level graphics API
// Window management API (basic functions in kernel.h)
// window_create and window_destroy are defined in kernel.h with different signatures
// Using kernel.h versions
void window_show(window_id_t window);
void window_hide(window_id_t window);
void window_move(window_id_t window, int x, int y);
void window_resize(window_id_t window, int width, int height);
bool window_is_visible(window_id_t window);

// Drawing functions with context
void graphics_begin_frame(window_id_t window, graphics_context_t* ctx);
void graphics_end_frame(window_id_t window);

// Context-based drawing (no window parameter needed)
void graphics_clear(const graphics_context_t* ctx);
void graphics_draw_rect(const graphics_context_t* ctx, int x, int y, int w, int h, uint32_t color);
void graphics_draw_circle(const graphics_context_t* ctx, int cx, int cy, int radius, uint32_t color);
void graphics_draw_line(const graphics_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color);
void graphics_draw_text(const graphics_context_t* ctx, int x, int y, const char* text, uint32_t color);

// Utility graphics functions
void graphics_draw_gradient_rect(const graphics_context_t* ctx, int x, int y, int w, int h,
                                uint32_t color1, uint32_t color2, bool vertical);
void graphics_draw_border(const graphics_context_t* ctx, int x, int y, int w, int h,
                         uint32_t color, int thickness);

// ============================================================================
// ENHANCED EVENT SYSTEM API
// ============================================================================

// Event types and structures (defined in kernel.h)
// event_type_t is defined in kernel.h

// event_t is defined in kernel.h

// Event queue API
typedef struct event_queue event_queue_t;

event_queue_t* event_queue_create(void);
void event_queue_destroy(event_queue_t* queue);
bool event_queue_poll(event_queue_t* queue, event_t* event);
bool event_queue_wait(event_queue_t* queue, event_t* event, uint32_t timeout_ms);
bool event_queue_push(event_queue_t* queue, const event_t* event);

// Global event handling
void event_send_global(const event_t* event);
void event_send_to_process(pid_t pid, const event_t* event);
void event_send_to_window(window_id_t window, const event_t* event);

// Event loop helper
typedef bool (*event_handler_t)(const event_t* event, void* user_data);

void event_loop_run(event_handler_t handler, void* user_data);
void event_loop_quit(void);

// ============================================================================
// ENHANCED ERROR HANDLING
// ============================================================================

// Error codes are defined earlier in this file (lines 85-104)

// Result type for better error handling
typedef struct {
    bool success;
    kernel_error_t error;
    union {
        void* ptr_result;
        int64_t int_result;
        uint64_t uint_result;
    } data;
} result_t;

#define RESULT_OK(value) (result_t){.success = true, .error = KERNEL_SUCCESS, .data.ptr_result = (void*)(value)}
#define RESULT_ERROR(code) (result_t){.success = false, .error = (code), .data.ptr_result = NULL}

// ============================================================================
// SYSTEM INFORMATION AND UTILITIES
// ============================================================================

// System info with friendly accessors
typedef struct system_info {
    const char* kernel_name;
    const char* kernel_version;
    uint64_t uptime_ms;
    uint32_t process_count;
    memory_stats_t memory;
    struct {
        uint32_t width, height, bpp;
        uint32_t refresh_rate;
    } display;
} system_info_t;

void system_get_info(system_info_t* info);

// Time and timing utilities
uint64_t time_monotonic_ms(void);
uint64_t time_realtime_ms(void);
void sleep_ms(uint32_t milliseconds);
void sleep_us(uint32_t microseconds);

// Random number generation
uint32_t random_uint32(void);
void random_bytes(void* buffer, size_t size);

// String utilities
size_t str_copy(char* dst, const char* src, size_t max_len);
int str_compare(const char* s1, const char* s2);
size_t str_length(const char* str);
char* str_duplicate(const char* str);

// ============================================================================
// DEBUGGING AND LOGGING API
// ============================================================================

// Logging levels
typedef enum log_level {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
} log_level_t;

// Enhanced logging with formatting
void log_message(log_level_t level, const char* format, ...);
void log_set_level(log_level_t level);

// Debug assertions and diagnostics
#define ASSERT(condition) do { \
    if (!(condition)) { \
        log_message(LOG_CRITICAL, "Assertion failed: %s at %s:%d", #condition, __FILE__, __LINE__); \
        while(1); /* Halt */ \
    } \
} while(0)

#define ASSERT_MSG(condition, msg) do { \
    if (!(condition)) { \
        log_message(LOG_CRITICAL, "Assertion failed: %s - %s at %s:%d", #condition, msg, __FILE__, __LINE__); \
        while(1); \
    } \
} while(0)

// Performance profiling
typedef struct profiler profiler_t;

profiler_t* profiler_create(const char* name);
void profiler_destroy(profiler_t* profiler);
void profiler_start(profiler_t* profiler);
uint64_t profiler_stop(profiler_t* profiler);
void profiler_reset(profiler_t* profiler);

// ============================================================================
// SUMMARY - WHAT THIS API PROVIDES
// ============================================================================

/*
This API transforms kernel development from low-level complexity to modern,
developer-friendly programming. Key improvements:

1. **Memory Management**: Automatic leak detection, RAII, memory pools
2. **Process Management**: Configuration-driven process creation, groups, environments
3. **File System**: RAII file handles, high-level operations, path utilities
4. **Graphics**: Context-based drawing, window management, advanced primitives
5. **Event System**: Queues, global messaging, event loops, type-safe structures
6. **Error Handling**: Rich error codes, result types, meaningful messages
7. **Utilities**: Time functions, random numbers, string helpers, profiling

This API enables developers to write kernel applications with the same productivity
as user-space programming, while maintaining the full power and control of kernel development.

Developers can now build:
- GUI applications with rich interfaces
- Multi-process systems with clean abstractions
- Event-driven architectures
- Performance-optimized graphics applications
- Enterprise-grade system software

All with dramatically reduced development time and complexity!
*/

#endif // API_H
