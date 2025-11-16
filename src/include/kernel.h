#ifndef _KERNEL_H
#define _KERNEL_H

#include "types.h"

/* Kernel version and configuration */
#define KERNEL_NAME "Base Kernel"
#define KERNEL_VERSION "0.1.0"
#define KERNEL_ARCH "x86_64"

/* Memory layout */
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#define PHYSICAL_MEMORY_LIMIT (128ULL * 1024ULL * 1024ULL * 1024ULL) /* 128GB */

/* Page size */
#define PAGE_SIZE 4096
#define PAGE_SIZE_BITS 12

/* Kernel stack size */
#define KERNEL_STACK_SIZE (64 * 1024)

/* Panic and assertion support */
#define PANIC(msg) kernel_panic(__FILE__, __LINE__, msg)
#define ASSERT(cond) do { if (!(cond)) PANIC("Assertion failed: " #cond); } while (0)

/* Debug output */
#define KDEBUG(fmt, ...) kernel_debug(fmt, ##__VA_ARGS__)
#define KINFO(fmt, ...) kernel_info(fmt, ##__VA_ARGS__)
#define KWARN(fmt, ...) kernel_warn(fmt, ##__VA_ARGS__)
#define KERROR(fmt, ...) kernel_error(fmt, ##__VA_ARGS__)

/* Function attributes */
#define __INIT __attribute__((section(".text.start")))
#define __NORETURN __attribute__((noreturn))
#define __ALIGNED(x) __attribute__((aligned(x)))
#define __PACKED __attribute__((packed))
#define __UNUSED __attribute__((unused))

/* External declarations for linker symbols */
extern char _kernel_start[];
extern char _kernel_end[];
extern char _kernel_stack_bottom[];
extern char _kernel_stack_top[];

/* Multiboot information from boot loader */
extern uint32_t multiboot_magic;
extern uint32_t multiboot_info;

/* Initialization functions */
void kernel_early_init(void);
void kernel_init(void);
void kernel_main(void) __NORETURN;

/* Panic and error handling */
__NORETURN void kernel_panic(const char* file, int line, const char* msg);

/* Debug output functions */
void kernel_debug(const char* fmt, ...);
void kernel_info(const char* fmt, ...);
void kernel_warn(const char* fmt, ...);
void kernel_error(const char* fmt, ...);

/* Memory operations */
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
int memcmp(const void* a, const void* b, size_t len);
size_t strlen(const char* str);

/* String operations */
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);

/* I/O operations */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* Initialization stub functions */
void serial_init(void);
void vga_init(void);
void vga_putc(char c);
void gdt_init(void);
void idt_init(void);
void paging_init(void);
void pmm_init(void);
uintptr_t pmm_alloc_pages(size_t num_pages);
void pmm_free_pages(uintptr_t addr, size_t num_pages);
bool vmm_map_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint32_t flags);
bool vmm_unmap_page(uintptr_t virtual_addr);
void kheap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void pic_init(void);
void pic_eoi(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
void timer_init(void);
void keyboard_init(void);
void keyboard_handler(void);
void scheduler_init(void);
uint64_t scheduler_get_current_task_id(void);
void scheduler_yield(void);
void scheduler_terminate(void);
void scheduler_tick(void);
void timer_tick(void);

/* Serial I/O */
void serial_write(char c);

/* Syscall interface */
int64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);

/* Filesystem interface */
void vfs_init(void);

/* List utilities (from VFS) */
void INIT_LIST_HEAD(struct list_head* list);

#endif /* _KERNEL_H */
