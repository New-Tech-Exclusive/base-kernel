#include "vfs.h"
#include "kernel.h"
#include "errno.h"

/*
 * Virtual File System (VFS) Core
 * Linux-compatible VFS implementation
 */

// Global filesystem list
struct file_system_type* file_systems = NULL;

// Initialize VFS
void vfs_init(void)
{
    KINFO("Initializing Virtual File System...");

    // Register basic filesystems (to be implemented)
    // TODO: register_procfs();
    // TODO: register_devfs();

    KINFO("VFS initialized");
}

// Register a filesystem type
int register_filesystem(struct file_system_type* fs)
{
    if (!fs || !fs->name) {
        return -EINVAL;
    }

    // Check if already registered
    struct file_system_type* temp = file_systems;
    while (temp) {
        if (strcmp(temp->name, fs->name) == 0) {
            return -EBUSY;
        }
        temp = temp->next;
    }

    // Add to list
    fs->next = file_systems;
    file_systems = fs;

    KINFO("Registered filesystem: %s", fs->name);
    return 0;
}

// Unregister a filesystem type
int unregister_filesystem(struct file_system_type* fs)
{
    if (!fs) {
        return -EINVAL;
    }

    struct file_system_type** p = &file_systems;
    while (*p && *p != fs) {
        p = &(*p)->next;
    }

    if (!*p) {
        return -EINVAL; // Not found
    }

    *p = fs->next;
    KINFO("Unregistered filesystem: %s", fs->name);
    return 0;
}

// Allocate a new inode
struct inode* vfs_alloc_inode(struct super_block* sb)
{
    if (!sb || !sb->s_op || !sb->s_op->alloc_inode) {
        return NULL;
    }

    return sb->s_op->alloc_inode(sb);
}

// Free an inode
void vfs_destroy_inode(struct inode* inode)
{
    if (!inode || !inode->i_sb || !inode->i_sb->s_op ||
        !inode->i_sb->s_op->destroy_inode) {
        return;
    }

    inode->i_sb->s_op->destroy_inode(inode);
}

// Get an inode from a dentry
struct inode* vfs_dentry_iget(struct dentry* dentry)
{
    if (!dentry) {
        return NULL;
    }
    return dentry->d_inode;
}

// Create a new file structure
struct file* vfs_alloc_file(void)
{
    struct file* file = kmalloc(sizeof(struct file));
    if (!file) {
        return NULL;
    }

    memset(file, 0, sizeof(struct file));
    return file;
}

// Free a file structure
void vfs_free_file(struct file* file)
{
    if (file) {
        kfree(file);
    }
}

// Create a new inode with operations
struct inode* new_inode(struct super_block* sb)
{
    struct inode* inode = vfs_alloc_inode(sb);
    if (!inode) {
        return NULL;
    }

    // Initialize basic fields
    inode->i_sb = sb;
    inode->i_ino = 0; // To be set by filesystem
    inode->i_mode = 0;
    inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_size = 0;
    inode->i_nlink = 1;
    inode->i_blocks = 0;

    // Set timestamps (simplified - use current time)
    inode->i_atime = 0;
    inode->i_mtime = 0;
    inode->i_ctime = 0;

    inode->i_op = NULL;
    inode->i_fop = NULL;
    inode->i_private = NULL;

    return inode;
}

// Create a new dentry
struct dentry* d_alloc(struct dentry* parent, const struct qstr* name)
{
    struct dentry* dentry = kmalloc(sizeof(struct dentry));
    if (!dentry) {
        return NULL;
    }

    memset(dentry, 0, sizeof(struct dentry));

    if (name) {
        dentry->d_name = kmalloc(name->len + 1);
        if (dentry->d_name) {
            memcpy(dentry->d_name, name->name, name->len);
            dentry->d_name[name->len] = '\0';
            dentry->d_name_len = name->len;
        }
    }

    dentry->d_parent = parent;

    // Initialize list heads
    INIT_LIST_HEAD(&dentry->d_child);
    INIT_LIST_HEAD(&dentry->d_subdirs);

    return dentry;
}

// Free a dentry
void d_free(struct dentry* dentry)
{
    if (!dentry) {
        return;
    }

    if (dentry->d_name) {
        kfree(dentry->d_name);
    }
    kfree(dentry);
}

// Simple path walk (basic implementation)
struct dentry* vfs_path_lookup(const char* pathname)
{
    // Simplified - for now, only handle absolute paths from root
    if (pathname[0] != '/') {
        return NULL;
    }

    // For now, return NULL (needs filesystem implementation)
    return NULL;
}

// Initialize list head (utility function)
void INIT_LIST_HEAD(struct list_head* list)
{
    list->next = list;
    list->prev = list;
}

// Filesystem mount point structure (simplified)
struct vfsmount {
    struct dentry* mnt_root;
    struct super_block* mnt_sb;
    // Additional fields...
};
