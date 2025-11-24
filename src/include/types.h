#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Standard POSIX-like types
typedef int64_t ssize_t;
typedef int pid_t;
typedef uint64_t off_t;
typedef uint32_t dev_t;
typedef uint32_t umode_t;
typedef uint32_t key_t;

// Legacy/Compatibility types (if any)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Kernel types
typedef int64_t loff_t;
typedef unsigned int gfp_t;

// Linked list structures
struct list_head {
    struct list_head *next, *prev;
};

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

// Lock debugging (dummy for now)
struct lock_class_key { };

#endif // TYPES_H
