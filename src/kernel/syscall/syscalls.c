#include "kernel.h"
#include "syscalls.h"

/*
 * System Call Implementation
 * Linux-compatible system calls for x86_64
 */

// Error codes
#define ENOSYS          38      // Function not implemented
#define ENOMEM          12      // Out of memory
#define EINVAL          22      // Invalid argument
#define EACCES          13      // Permission denied
#define EIO             5       // I/O error
#define ENOENT          2       // No such file or directory

// Commonly used return values
#define SUCCESS         0

// Define types for shared memory (simplified)
typedef int key_t;       // IPC key type
typedef int pid_t;       // Process ID type

// Shared memory structs
struct shmid_ds {
    int dummy; // Placeholder
};

// Shared memory constants
#define IPC_PRIVATE ((key_t)0)    // Private key for new segments
#define IPC_RMID    0             // Remove segment
#define EMFILE      24            // Too many open files
#define SHM_DEST    01000         // Destroy after last detach

/*
 * Shared Memory Implementation
 * POSIX-compliant shared memory segments
 */

// Shared memory segment structure
typedef struct {
    int shmid;          // Shared memory ID
    void* addr;         // Attached address in kernel space
    size_t size;        // Size of segment
    int key;            // Key for IPC
    pid_t creator;      // PID of creator
    int ref_count;      // Number of processes attached
    int flags;          // Permissions and flags
} shm_segment_t;

// Shared memory table (simple array for demo)
#define MAX_SHM_SEGMENTS 16
static shm_segment_t shm_segments[MAX_SHM_SEGMENTS];
static int next_shmid = 1;

// Process attachment tracking
typedef struct {
    int shmid;
    void* addr;
    size_t size;
} shm_attachment_t;

#define MAX_SHM_ATTACHMENTS 8
static shm_attachment_t shm_attachments[MAX_SHM_ATTACHMENTS];

// Event system
extern int64_t sys_event_create_queue(void);
extern int64_t sys_event_destroy_queue(int queue_id);
extern int64_t sys_event_get_next(int queue_id, void* event_out);

// Framebuffer/display system
extern int64_t sys_get_display_info(void* info);
extern int64_t sys_window_create(int x, int y, int width, int height);
extern int64_t sys_window_destroy(int window_id);
extern int64_t sys_window_composite(int window_id);
extern int64_t sys_framebuffer_access(int window_id, void* buffer, void* width, void* height);
extern int64_t sys_draw_rect(int window_id, int x, int y, int w, int h, uint32_t color);
extern int64_t sys_draw_circle(int window_id, int center_x, int center_y, int radius, uint32_t color);

// System call implementations

// Shared memory get (shmget)
int64_t sys_shmget(key_t key, size_t size, int shmflg)
{
    // IPC_PRIVATE creates new segment, key values are ignored for now
    if (key != IPC_PRIVATE) {
        // For demo, key-based lookup not implemented
        return -ENOSYS;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].shmid == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        return -ENOMEM; // No free slots
    }

    // Allocate kernel memory for the segment
    void* addr = kmalloc(size);
    if (!addr) {
        return -ENOMEM;
    }

    // Initialize segment
    shm_segments[slot].shmid = next_shmid++;
    shm_segments[slot].addr = addr;
    shm_segments[slot].size = size;
    shm_segments[slot].key = key;
    shm_segments[slot].creator = 0; // Kernel process
    shm_segments[slot].ref_count = 0;
    shm_segments[slot].flags = shmflg;

    KDEBUG("Created shared memory segment %d, size %lu bytes",
           shm_segments[slot].shmid, size);

    return shm_segments[slot].shmid;
}

// Shared memory attach (shmat)
void* sys_shmat(int shmid, const void* shmaddr, int shmflg)
{
    // Find the segment
    shm_segment_t* segment = NULL;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].shmid == shmid) {
            segment = &shm_segments[i];
            break;
        }
    }

    if (!segment) {
        return (void*)-EINVAL; // Invalid shmid
    }

    // For demo, we'll map to a fixed virtual address range
    // In real implementation, this would involve page table modifications

    // Find free attachment slot
    int attach_slot = -1;
    for (int i = 0; i < MAX_SHM_ATTACHMENTS; i++) {
        if (shm_attachments[i].shmid == 0) {
            attach_slot = i;
            break;
        }
    }

    if (attach_slot == -1) {
        return (void*)-EMFILE; // Too many attachments
    }

    // Create virtual mapping (simplified - just return kernel address for now)
    // Real implementation would map pages into process address space
    void* virtual_addr = segment->addr; // Use kernel address as virtual for demo

    shm_attachments[attach_slot].shmid = shmid;
    shm_attachments[attach_slot].addr = virtual_addr;
    shm_attachments[attach_slot].size = segment->size;

    segment->ref_count++;

    KDEBUG("Attached to shared memory segment %d at address 0x%lx",
           shmid, (uintptr_t)virtual_addr);

    return virtual_addr;
}

// Shared memory control (shmctl)
int64_t sys_shmctl(int shmid, int cmd, struct shmid_ds* buf)
{
    // Simplified implementation - only support IPC_RMID for demo
    if (cmd == IPC_RMID) {
        for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
            if (shm_segments[i].shmid == shmid) {
                // Mark for destruction when refcount reaches 0
                shm_segments[i].flags |= SHM_DEST; // Would need to define this
                KDEBUG("Marked shared memory segment %d for destruction", shmid);
                return 0;
            }
        }
        return -EINVAL; // Invalid shmid
    }

    // Other commands not implemented
    return -ENOSYS;
}

// Shared memory detach (shmdt)
int64_t sys_shmdt(const void* shmaddr)
{
    // Find attachment and detach
    for (int i = 0; i < MAX_SHM_ATTACHMENTS; i++) {
        if (shm_attachments[i].addr == shmaddr) {
            int shmid = shm_attachments[i].shmid;

            // Clear attachment
            shm_attachments[i].shmid = 0;
            shm_attachments[i].addr = NULL;

            // Decrease refcount
            for (int j = 0; j < MAX_SHM_SEGMENTS; j++) {
                if (shm_segments[j].shmid == shmid) {
                    shm_segments[j].ref_count--;
                    if (shm_segments[j].ref_count == 0 &&
                        (shm_segments[j].flags & SHM_DEST)) {
                        // Free segment
                        kfree(shm_segments[j].addr);
                        memset(&shm_segments[j], 0, sizeof(shm_segment_t));
                        KDEBUG("Destroyed shared memory segment %d", shmid);
                    }
                    break;
                }
            }

            KDEBUG("Detached from shared memory at address 0x%lx", (uintptr_t)shmaddr);
            return 0;
        }
    }

    return -EINVAL; // Address not attached
}

// File operations (will be implemented with VFS later)
int64_t sys_read(uint64_t fd, char* buf, size_t count)
{
    // For now, only handle stdin (fd 0) via keyboard or serial
    return -ENOSYS;
}

int64_t sys_write(uint64_t fd, const char* buf, size_t count)
{
    // Handle stdout (fd 1) and stderr (fd 2) via serial
    if (fd == 1 || fd == 2) {
        // Write to serial console
        for (size_t i = 0; i < count; i++) {
            serial_write(buf[i]);
        }
        return count;
    }
    return -ENOSYS;
}

int64_t sys_open(const char* filename, int flags, umode_t mode)
{
    return -ENOSYS;
}

int64_t sys_close(uint64_t fd)
{
    return -ENOSYS;
}

int64_t sys_lseek(uint64_t fd, off_t offset)
{
    return -ENOSYS;
}

// Memory management
int64_t sys_brk(unsigned long brk)
{
    // Basic heap allocation - very simplified
    static unsigned long current_brk = 0;
    if (brk == 0) {
        return current_brk;
    }
    if (brk > current_brk) {
        // Allocate more - simplistic
        size_t needed = brk - current_brk;
        void* new_mem = kmalloc(needed);
        if (!new_mem) {
            return -ENOMEM;
        }
        current_brk = brk;
    }
    return current_brk;
}

int64_t sys_mmap(unsigned long addr, unsigned long len, unsigned long prot,
                unsigned long flags, unsigned long fd, unsigned long off)
{
    return -ENOSYS;
}

int64_t sys_munmap(unsigned long addr, size_t len)
{
    return -ENOSYS;
}

// Process management
int64_t sys_getpid(void)
{
    return scheduler_get_current_task_id();
}

int64_t sys_exit(int error_code)
{
    KINFO("Process exiting with code %d", error_code);
    scheduler_terminate();
    return 0;  // Should not return
}

int64_t sys_execve(const char* filename, const char* const argv[],
                const char* const envp[])
{
    return -ENOSYS;
}

int64_t sys_fork(void)
{
    return scheduler_create_task_fork();
}

int64_t sys_wait4(pid_t pid, int* stat_addr, int options)
{
    return -ENOSYS;
}

int64_t sys_kill(pid_t pid, int sig)
{
    return -ENOSYS;
}

// Miscellaneous
int64_t sys_uname(struct utsname* buf)
{
    return -ENOSYS;
}

int64_t sys_yield(void)
{
    scheduler_yield();
    return 0;
}

int64_t sys_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    return -ENOSYS;
}

// System information
int64_t sys_sysinfo(struct sysinfo* info)
{
    return -ENOSYS;
}

// System call table (indexed by syscall number)
syscall_handler_t sys_call_table[] = {
    [SYS_read]         = (syscall_handler_t)sys_read,
    [SYS_write]        = (syscall_handler_t)sys_write,
    [SYS_open]         = (syscall_handler_t)sys_open,
    [SYS_close]        = (syscall_handler_t)sys_close,
    [SYS_lseek]        = (syscall_handler_t)sys_lseek,
    [SYS_brk]          = (syscall_handler_t)sys_brk,
    [SYS_mmap]         = (syscall_handler_t)sys_mmap,
    [SYS_munmap]       = (syscall_handler_t)sys_munmap,
    [SYS_shmget]       = (syscall_handler_t)sys_shmget,
    [SYS_shmat]        = (syscall_handler_t)sys_shmat,
    [SYS_shmctl]       = (syscall_handler_t)sys_shmctl,
    [SYS_shmdt]        = (syscall_handler_t)sys_shmdt,
    [SYS_getpid]       = (syscall_handler_t)sys_getpid,
    [SYS_exit]         = (syscall_handler_t)sys_exit,
    [SYS_execve]       = (syscall_handler_t)sys_execve,
    [SYS_fork]         = (syscall_handler_t)sys_fork,
    [SYS_event_create_queue]  = (syscall_handler_t)sys_event_create_queue,
    [SYS_event_destroy_queue] = (syscall_handler_t)sys_event_destroy_queue,
    [SYS_event_get_next]      = (syscall_handler_t)sys_event_get_next,
    [SYS_get_display_info]    = (syscall_handler_t)sys_get_display_info,
    [SYS_window_create]       = (syscall_handler_t)sys_window_create,
    [SYS_window_destroy]      = (syscall_handler_t)sys_window_destroy,
    [SYS_window_composite]    = (syscall_handler_t)sys_window_composite,
    [SYS_framebuffer_access]  = (syscall_handler_t)sys_framebuffer_access,
    [SYS_draw_rect]           = (syscall_handler_t)sys_draw_rect,
    [SYS_draw_circle]         = (syscall_handler_t)sys_draw_circle,
    [SYS_wait4]        = (syscall_handler_t)sys_wait4,
    [SYS_kill]         = (syscall_handler_t)sys_kill,
    [SYS_uname]        = (syscall_handler_t)sys_uname,
    [SYS_sched_yield]  = (syscall_handler_t)sys_yield,
    [SYS_gettimeofday] = (syscall_handler_t)sys_gettimeofday,
    [SYS_sysinfo]      = (syscall_handler_t)sys_sysinfo,
    // Add more system calls as implemented...
};

// System call dispatcher
int64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    if (num >= sizeof(sys_call_table) / sizeof(sys_call_table[0])) {
        return -ENOSYS;
    }

    syscall_handler_t handler = sys_call_table[num];
    if (!handler) {
        return -ENOSYS;
    }

    return handler(arg1, arg2, arg3, arg4, arg5, arg6);
}
