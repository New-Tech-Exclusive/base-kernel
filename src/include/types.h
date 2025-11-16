#ifndef _TYPES_H
#define _TYPES_H

/* Basic type definitions for kernel */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef uint64_t uintptr_t;
typedef uint64_t physaddr_t;

/* Process ID type */
typedef int32_t pid_t;

/* File modes */
typedef uint16_t umode_t;

/* File offset */
typedef int64_t off_t;
typedef int64_t loff_t;

/* Device type */
typedef uint32_t dev_t;

/* Linux-style list head and related structures */
struct list_head {
    struct list_head *next, *prev;
};

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

struct lock_class_key {
    int dummy; /* Placeholder for locking mechanisms */
};

/* GFP (Get Free Page) allocation flags */
typedef unsigned int gfp_t;
#define GFP_KERNEL  0x0001  /* Normal kernel allocation */
#define GFP_ATOMIC  0x0002  /* Atomic allocation (no sleep) */
#define GFP_USER    0x0004  /* User page allocation */
#define GFP_IO      0x0008  /* Can do I/O */
#define GFP_FS      0x0010  /* Can do filesystem operations */

/* Basic qstr for VFS */
struct qstr {
    const unsigned char *name;
    size_t len;
    uint32_t hash;
};

/* Boolean type */
typedef enum { false = 0, true = 1 } bool;

/* NULL pointer */
#define NULL ((void*)0)

/* Structure forward declarations for system calls */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char _f[0];  /* Padding to 64 bytes */
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* Common macros */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))

#endif /* _TYPES_H */
