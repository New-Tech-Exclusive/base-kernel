#ifndef _VFS_H
#define _VFS_H

#include "types.h"

/*
 * Virtual File System (VFS) Layer
 * Linux-compatible inode, dentry, superblock structures
 */

// File types
typedef enum {
    VFS_TYPE_REGULAR,
    VFS_TYPE_DIR,
    VFS_TYPE_CHARDEV,
    VFS_TYPE_BLOCKDEV,
    VFS_TYPE_PIPE,
    VFS_TYPE_SOCKET,
    VFS_TYPE_SYMLINK
} vfs_file_type_t;

// VFS inode structure (Linux-style)
struct inode {
    uint64_t i_ino;                    // Inode number
    uint32_t i_mode;                   // File mode
    uint32_t i_uid;                    // Owner UID
    uint32_t i_gid;                    // Owner GID
    uint64_t i_size;                   // File size
    uint64_t i_atime;                  // Access time
    uint64_t i_mtime;                  // Modification time
    uint64_t i_ctime;                  // Change time
    uint32_t i_nlink;                  // Number of hard links
    uint32_t i_blocks;                 // Number of 512-byte blocks

    struct super_block* i_sb;          // Pointer to superblock

    // Operations
    const struct inode_operations* i_op;
    const struct file_operations* i_fop;

    void* i_private;                   // Private data
};

// VFS dentry structure (directory entry)
struct dentry {
    uint32_t d_flags;                  // Dentry flags
    struct inode* d_inode;             // Associated inode
    struct dentry* d_parent;           // Parent directory
    struct list_head d_child;          // Child list (for directory cache)
    struct list_head d_subdirs;        // Subdirectories list

    char* d_name;                      // Name
    unsigned short d_name_len;

    unsigned char d_type;              // File type (same as dirent)

    // Operations
    const struct dentry_operations* d_op;

    void* d_private;                   // Private data
};

// VFS superblock structure
struct super_block {
    uint64_t s_blocksize;              // Block size
    uint32_t s_flags;                  // Superblock flags

    uint64_t s_magic;                  // Filesystem magic number
    uint64_t s_maxbytes;               // Maximum file size

    struct inode* s_root;              // Root inode
    struct dentry* s_root_dentry;      // Root dentry

    const struct super_operations* s_op;

    void* s_fs_info;                   // Filesystem private info
    char s_id[32];                     // Identifier
};

// VFS file structure
struct file {
    unsigned int f_mode;               // File mode
    unsigned int f_flags;              // File flags
    uint64_t f_pos;                    // File position

    struct inode* f_inode;             // Associated inode
    struct dentry* f_dentry;           // Associated dentry

    const struct file_operations* f_op;

    void* private_data;                // Private data
};

// File operations (like Linux)
struct file_operations {
    ssize_t (*read)(struct file*, char*, size_t, loff_t*);
    ssize_t (*write)(struct file*, const char*, size_t, loff_t*);

    int (*open)(struct inode*, struct file*);
    int (*release)(struct inode*, struct file*);

    // Additional operations (simplified for now)
    loff_t (*llseek)(struct file*, loff_t, int);
    int (*ioctl)(struct inode*, struct file*, unsigned int, unsigned long);
};

// Inode operations
struct inode_operations {
    int (*create)(struct inode*, struct dentry*, umode_t, bool);
    struct dentry* (*lookup)(struct inode*, struct dentry*, unsigned int);

    // Additional operations (simplified)
    int (*link)(struct dentry*, struct inode*, struct dentry*);
    int (*unlink)(struct inode*, struct dentry*);
    int (*symlink)(struct inode*, struct dentry*, const char*);
    int (*mkdir)(struct inode*, struct dentry*, umode_t);
    int (*rmdir)(struct inode*, struct dentry*);

    int (*mknod)(struct inode*, struct dentry*, umode_t, dev_t);
    int (*rename)(struct inode*, struct dentry*, struct inode*, struct dentry*);

    int (*permission)(struct inode*, int);
};

// Dentry operations
struct dentry_operations {
    int (*d_revalidate)(struct dentry*, unsigned int);
    int (*d_weak_revalidate)(struct dentry*, unsigned int);
    int (*d_hash)(const struct dentry*, struct qstr*);
    int (*d_compare)(const struct dentry*, unsigned int, const char*,
                    const struct qstr*);

    // Additional
    int (*d_delete)(const struct dentry*);
    void (*d_release)(struct dentry*);
    void (*d_prune)(struct dentry*);
    void (*d_iput)(struct dentry*, struct inode*);

    char* (*d_dname)(struct dentry*, char*, int);
};

// Superblock operations
struct super_operations {
    struct inode* (*alloc_inode)(struct super_block* sb);
    void (*destroy_inode)(struct inode*);

    void (*dirty_inode)(struct inode*, int);

    int (*write_inode)(struct inode*, struct writeback_control* wbc);
    int (*drop_inode)(struct inode*);

    void (*evict_inode)(struct inode*);

    void (*put_super)(struct super_block*);

    int (*sync_fs)(struct super_block*, int);
    int (*freeze_super)(struct super_block*);
    int (*unfreeze_super)(struct super_block*);

    int (*statfs)(struct super_block*, struct kstatfs*);

    int (*remount_fs)(struct super_block*, int*, char*);

    void (*umount_begin)(struct super_block*);

    int (*show_options)(struct seq_file*, struct vfsmount*);

    int (*show_devname)(struct seq_file*, struct vfsmount*);

    int (*show_path)(struct seq_file*, struct vfsmount*);

    int (*show_stats)(struct seq_file*, struct vfsmount*);

    ssize_t (*quota_read)(struct super_block*, int, char*, size_t, loff_t);
    ssize_t (*quota_write)(struct super_block*, int, const char*, size_t, loff_t);

    struct dquot** (*get_dquots)(struct inode*);

    int (*bdev_try_to_free_page)(struct super_block*, struct page*, gfp_t);
};

// Filesystem type structure
struct file_system_type {
    const char* name;                          // Filesystem name
    int fs_flags;

    struct dentr_y* (*mount)(struct file_system_type*, int, const char*, void*);

    void (*kill_sb)(struct super_block*);

    struct module* owner;

    struct file_system_type* next;

    struct hlist_head fs_supers;

    struct lock_class_key s_lock_key;
    struct lock_class_key s_umount_key;
    struct lock_class_key s_vfs_rename_key;
    struct lock_class_key i_lock_key;
    struct lock_class_key i_mutex_key;
    struct lock_class_key i_mutex_dir_key;

    struct list_head fs_list;
};

// External declarations
extern struct file_system_type* file_systems;

// Filesystem registration functions
int register_filesystem(struct file_system_type* fs);
int unregister_filesystem(struct file_system_type* fs);

// Path resolution
struct path {
    struct vfsmount* mnt;
    struct dentry* dentry;
};

// Simplified types for basic implementation (defined in types.h)

// Forward declarations for dependencies
struct writeback_control;
struct kstatfs;
struct vfsmount;
struct seq_file;
struct list_head;
struct qstr;
struct dquot;
struct page;
struct module;
struct hlist_head;
struct lock_class_key;

#endif /* _VFS_H */
