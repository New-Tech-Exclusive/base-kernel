#include "kernel.h"
#include "syscalls.h"

/*
 * System Call Implementation
 * Linux-compatible system calls for x86_64
 */

// Error codes
#define ENOSYS          38      // Function not implemented
#define EINVAL          22      // Invalid argument
#define EACCES          13      // Permission denied
#define EIO             5       // I/O error
#define ENOENT          2       // No such file or directory

// Commonly used return values
#define SUCCESS         0

// System call implementations

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
    return -ENOSYS;
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
    return -ENOSYS;
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
    [SYS_getpid]       = (syscall_handler_t)sys_getpid,
    [SYS_exit]         = (syscall_handler_t)sys_exit,
    [SYS_execve]       = (syscall_handler_t)sys_execve,
    [SYS_fork]         = (syscall_handler_t)sys_fork,
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
