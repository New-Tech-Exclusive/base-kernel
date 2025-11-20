/*
 * Kernel Header File
 * Core kernel types and functions
 */

#ifndef KERNEL_H
#define KERNEL_H

#include <stdarg.h>
#include "types.h"
// Note: io.h is included by individual files that need I/O functions

// ============================================================================
// KERNEL DEBUGGING AND PRINTING
// ============================================================================

// Print functions (implemented in print.c)
void kprintf(const char* fmt, ...);
void kvprintf(const char* fmt, va_list args);

// Debug macros
#define KDEBUG(fmt, ...) do { kprintf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)
#define KINFO(fmt, ...)  do { kprintf("[INFO]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define KWARN(fmt, ...)  do { kprintf("[WARN]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define KERROR(fmt, ...) do { kprintf("[ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

// Core memory allocation (implemented in kheap.c)
void* kmalloc(size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);
void* kmalloc_tracked(size_t size, const char* tag);
void kfree_tracked(void* ptr);

// Physical memory management (implemented in pmm.c)
uintptr_t pmm_alloc_page(void);
uintptr_t pmm_alloc_pages(size_t num_pages);
void pmm_free_page(void* page);
void pmm_free_pages(uintptr_t addr, size_t num_pages);
void pmm_init(void);
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
void pmm_get_stats(uint64_t* req, uint64_t* fail, uint64_t* hit, uint64_t* frag);

// Memory statistics
typedef struct {
    size_t total_allocated;
    size_t peak_usage;
    size_t allocations;
    size_t deallocations;
} memory_stats_t;

// Multiboot information (passed from bootloader)
extern uint32_t multiboot_info;
extern uint32_t multiboot_magic;

#define PAGE_SIZE 4096
#define PHYSICAL_MEMORY_LIMIT 0xFFFFFFFF // 4GB limit for now
#define EVENT_QUEUE_SIZE 128
#define MAX_WM_WINDOWS 32

// ============================================================================
// SCHEDULER AND PROCESSES
// ============================================================================

// Process states
typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

// Process information
typedef struct scheduler_task_info {
    pid_t pid;
    const char* name;
    task_state_t state;
    size_t stack_size;
    int priority;
    uint64_t creation_time_ms;
    uint64_t cpu_time_ms;
} scheduler_task_info_t;

// Forward declaration for VMA
struct vma;

// Virtual memory context
typedef struct vm_context {
    struct vma* vma_list;         // List of VMAs
    uintptr_t brk;                // Current program break (heap end)
    uintptr_t mmap_base;          // Base for mmap allocations
    uint64_t* page_dir;           // Page directory pointer (PML4)
} vm_context_t;

// Process entry point
typedef void (*process_entry_t)(void*);

// Panic macro
#define PANIC(msg) kernel_panic(__FILE__, __LINE__, msg)
void kernel_panic(const char* file, int line, const char* msg);

// Scheduler functions (implemented in scheduler.c)
pid_t scheduler_create_task(void (*entry)(void*), void* arg, size_t stack_size, int priority, const char* name);
int scheduler_kill_task(pid_t pid);
void scheduler_yield(void);
void schedule_yield(void);  // Alias
void scheduler_schedule(void); // Main scheduler function
void scheduler_tick(void);  // Timer tick handler for scheduler
pid_t scheduler_get_current_task_id(void);
int scheduler_get_task_state(pid_t pid);
int scheduler_get_task_info(pid_t pid, scheduler_task_info_t* info);
void scheduler_terminate(void);  // Terminate current task
pid_t scheduler_create_task_fork(void);  // Fork current task
void schedule_delay(uint32_t ms);  // Delay for milliseconds

// ============================================================================
// STRING FUNCTIONS
// ============================================================================

int vsnprintf(char* str, size_t size, const char* format, va_list ap);
int sprintf(char* str, const char* format, ...);

#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

// ============================================================================
// INTERRUPT HANDLING
// ============================================================================

// Interrupt management (implemented in interrupt.c)
// Note: interrupt_frame_t is defined locally in interrupt.c
void interrupt_init(void);
// interrupt_register_handler is not currently implemented

// ============================================================================
// TIMER MANAGEMENT
// ============================================================================

// Timer functions (implemented in timer.c)
void timer_init(void);
void timer_tick(void);
uint64_t timer_get_ticks(void);
uint64_t time_monotonic_ms(void);

// ============================================================================
// KEYBOARD INPUT
// ============================================================================

// Keyboard functions (implemented in keyboard.c)
void keyboard_init(void);
void keyboard_handler(void);

// ============================================================================
// SERIAL I/O
// ============================================================================

// Serial functions
uint8_t serial_read(void);
void serial_write(uint8_t byte);
void serial_write_string(const char* str);

// ============================================================================
// PCI MANAGEMENT
// ============================================================================

// PIC (Programmable Interrupt Controller) functions
void pic_init(void);
void pic_eoi(uint8_t irq);

// ============================================================================
// FILE SYSTEM AND VIRTUAL FILE SYSTEM
// ============================================================================

// VFS includes
#include "vfs.h"

// VFS functions (implemented in vfs.c)
int vfs_init(void);
int vfs_mount(const char* path, filesystem_t* fs);
int vfs_unmount(const char* path);

// File operations
int sys_open(const char* filename, int flags, umode_t mode);
size_t sys_read(uint64_t fd, char* buf, size_t count);
size_t sys_write(uint64_t fd, const char* buf, size_t count);
int64_t sys_lseek(uint64_t fd, off_t offset, int whence);
int sys_close(uint64_t fd);

// ============================================================================
// SHARED MEMORY
// ============================================================================

// System calls for shared memory
int64_t sys_shmget(key_t key, size_t size, int shmflg);
void* sys_shmat(int shmid, const void* shmaddr, int shmflg);
int64_t sys_shmdt(const void* shmaddr);
int64_t sys_shmctl(int shmid, int cmd, struct shmid_ds* buf);

// ============================================================================
// EVENT SYSTEM
// ============================================================================

// Event types
typedef enum {
    EVENT_TYPE_KEYBOARD,
    EVENT_TYPE_MOUSE,
    EVENT_TYPE_WINDOW,
    EVENT_TYPE_SYSTEM
} event_type_t;

// Basic event structure
typedef struct {
    event_type_t type;
    uint32_t timestamp;
    union {
        struct {
            uint32_t keycode;
            uint32_t modifiers;
            uint32_t state;
        } keyboard;
        struct {
            int32_t x, y;
            uint32_t buttons;
            int32_t wheel;
        } mouse;
    } data;
} event_t;

// Event functions (implemented in events.c)
int64_t sys_event_create_queue(void);
int64_t sys_event_destroy_queue(int64_t queue_id);
int64_t sys_event_get_next(event_t* event, uint64_t timeout);

// Event posting functions
extern int event_queue_keyboard(pid_t target_process, uint32_t keycode, uint32_t modifiers, uint32_t state);
extern int event_queue_mouse(pid_t target_process, int32_t x, int32_t y, uint32_t buttons, int32_t wheel);

// ============================================================================
// GRAPHICS AND FRAMEBUFFER
// ============================================================================

// Framebuffer functions (implemented in framebuffer.c)
int framebuffer_init(void);
void framebuffer_put_pixel(int x, int y, uint32_t color);
uint32_t framebuffer_get_pixel(int x, int y);
void framebuffer_fill_rect(int x, int y, int width, int height, uint32_t color);
void framebuffer_draw_text(int x, int y, const char* text, uint32_t color);
void framebuffer_draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void framebuffer_draw_circle(int sx, int sy, int radius, uint32_t color);

// Window management (implemented in framebuffer.c)
int window_create(int x, int y, int width, int height, pid_t owner_pid);
int window_destroy(int window_id);
volatile uint8_t* window_get_buffer(int window_id);
void window_composite(int window_id);

// Display server functions
void display_server_init(void);

// System calls for graphics
int64_t sys_framebuffer_access(void** framebuffer, uint32_t* width, uint32_t* height, uint32_t* bpp);
int64_t sys_window_create(int x, int y, int w, int h);
int64_t sys_window_destroy(int window_id);
int64_t sys_window_composite(int window_id);
int64_t sys_draw_rect(int window_id, int x, int y, int w, int h, uint32_t color);
int64_t sys_draw_circle(int window_id, int center_x, int center_y, int radius, uint32_t color);
int64_t sys_get_display_info(uint32_t* width, uint32_t* height, uint32_t* bpp);

// ============================================================================
// SYSTEM CALLS
// ============================================================================

// System call numbers
#define SYS_read              0
#define SYS_write             1
#define SYS_open              2
#define SYS_close             3
#define SYS_lseek             8
#define SYS_brk              12
#define SYS_mmap             9
#define SYS_munmap           11
#define SYS_shmget           29
#define SYS_shmat            30


// System call dispatcher (implemented in syscall.c)
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);

// ============================================================================
// STRING UTILITIES
// ============================================================================

// Kernel string functions (implemented in string.c)
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
char* strcat(char* dest, const char* src);

// ============================================================================
// TIME UTILITIES
// ============================================================================

// Time functions
uint64_t sys_get_ticks(void);

// ============================================================================
// KERNEL INITIALIZATION
// ============================================================================

// Main kernel entry point
void kernel_main(void);

#endif // KERNEL_H
