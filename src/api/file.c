/*
 * Enhanced File System API Implementation
 * Provides developer-friendly file operations with RAII
 */

#include "kernel.h"
#include "api.h"

// File operation constants (if not defined elsewhere)
#ifndef O_RDONLY
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_EXEC      0x1000
#endif

#ifndef SEEK_SET
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2
#endif

// ============================================================================
// FILE OPERATIONS
// ============================================================================

// Internal file structure - use different name to avoid conflict with vfs.h
struct api_file {
    int fd;          // File descriptor from syscall layer
    char path[256];  // Original path for debugging
    int mode_flags;  // Store mode as int flags
};

file_t* file_open(const char* path, file_open_mode_t mode) {
    if (!path) return NULL;

    // Convert our mode to syscall flags
    int syscall_flags = 0;
    int syscall_mode = 0;

    if (mode & FILE_MODE_READ) syscall_flags |= O_RDONLY;
    if (mode & FILE_MODE_WRITE) syscall_flags |= O_WRONLY;
    if (mode & FILE_MODE_EXECUTE) syscall_flags |= O_EXEC;
    if (mode & FILE_MODE_CREATE) {
        if (mode & FILE_MODE_WRITE) {
            syscall_flags |= O_CREAT;
        }
    }
    if (mode & FILE_MODE_TRUNCATE) syscall_flags |= O_TRUNC;
    if (mode & FILE_MODE_APPEND) syscall_flags |= O_APPEND;

    // Default permissions
    syscall_mode = 0644;

    int fd = sys_open(path, syscall_flags, syscall_mode);
    if (fd < 0) return NULL;

    file_t* file = kmalloc_tracked(sizeof(file_t), "file_struct");
    if (!file) {
        sys_close(fd);
        return NULL;
    }

    file->fd = fd;
    str_copy(file->path, path, sizeof(file->path));
    file->mode_flags = mode;

    return file;
}

void file_close(file_t* file) {
    if (!file) return;

    if (file->fd >= 0) {
        sys_close(file->fd);
        file->fd = -1;
    }

    kfree_tracked(file);
}

size_t file_read(file_t* file, void* buffer, size_t size) {
    if (!file || file->fd < 0 || !buffer || !size) return 0;

    return sys_read(file->fd, buffer, size);
}

size_t file_write(file_t* file, const void* buffer, size_t size) {
    if (!file || file->fd < 0 || !buffer || !size) return 0;

    return sys_write(file->fd, buffer, size);
}

int64_t file_seek(file_t* file, int64_t offset, int whence) {
    if (!file || file->fd < 0) return -1;

    return sys_lseek(file->fd, offset, whence);
}

size_t file_size(file_t* file) {
    if (!file || file->fd < 0) return 0;

    // Save current position
    int64_t current_pos = file_seek(file, 0, SEEK_CUR);
    if (current_pos < 0) return 0;

    // Seek to end to get size
    int64_t size = file_seek(file, 0, SEEK_END);
    if (size < 0) return 0;

    // Restore position
    file_seek(file, current_pos, SEEK_SET);

    return (size_t)size;
}

// ============================================================================
// HIGH-LEVEL FILE OPERATIONS
// ============================================================================

char* file_read_all(const char* path) {
    file_t* file = file_open(path, FILE_MODE_READ);
    if (!file) return NULL;

    size_t size = file_size(file);
    if (size == 0) {
        file_close(file);
        return str_duplicate("");
    }

    char* buffer = kmalloc_tracked(size + 1, "file_read_all");
    if (!buffer) {
        file_close(file);
        return NULL;
    }

    size_t read_bytes = file_read(file, buffer, size);
    file_close(file);

    if (read_bytes != size) {
        kfree_tracked(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

int file_write_all(const char* path, const char* content) {
    file_t* file = file_open(path, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    if (!file) return -1;

    int result = 0;
    if (content) {
        size_t content_len = str_length(content);
        size_t written = file_write(file, content, content_len);
        if (written != content_len) {
            result = -1;
        }
    }

    file_close(file);
    return result;
}

int file_copy(const char* src, const char* dst) {
    char* content = file_read_all(src);
    if (!content) return -1;

    int result = file_write_all(dst, content);
    kfree_tracked(content);

    return result;
}

int file_exists(const char* path) {
    file_t* file = file_open(path, FILE_MODE_READ);
    if (!file) return 0;

    file_close(file);
    return 1;
}

bool file_is_directory(const char* path) {
    // Basic implementation - we'd need stat syscall for full support
    // For now, assume known directories
    static const char* known_dirs[] = { "/", "/programs", "/data", "/tools" };

    for (size_t i = 0; i < ARRAY_SIZE(known_dirs); i++) {
        if (str_compare(path, known_dirs[i]) == 0) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

struct dir {
    char path[256];
    size_t entry_index;
    // In a real implementation, this would contain directory structure
};

// dir_entry_t is defined in api.h

dir_t* dir_open(const char* path) {
    if (!path) return NULL;

    // Check if path is accessible
    if (!file_is_directory(path)) {
        return NULL;
    }

    dir_t* dir = kmalloc_tracked(sizeof(dir_t), "dir_struct");
    if (!dir) return NULL;

    str_copy(dir->path, path, sizeof(dir->path));
    dir->entry_index = 0;

    return dir;
}

dir_entry_t* dir_read(dir_t* dir) {
    if (!dir) return NULL;

    // Simple implementation with hardcoded entries for demo
    static const char* fake_entries[5] = { "readme.txt", "config.txt", NULL };
    static char entry_names[5][64];
    static dir_entry_t fake_entry;

    const char* entries_to_show = NULL;

    if (str_compare(dir->path, "/") == 0) {
        static const char* root_entries[6] = { "programs/", "data/", "files.txt", "config.sys", NULL };
        entries_to_show = root_entries[dir->entry_index];
    } else if (str_compare(dir->path, "/programs") == 0) {
        static const char* prog_entries[5] = { "game.exe", "editor.exe", "tools/", NULL };
        entries_to_show = prog_entries[dir->entry_index];
    }

    if (!entries_to_show) {
        return NULL;
    }

    // Copy to static buffer for safety
    str_copy(entry_names[dir->entry_index], entries_to_show, sizeof(entry_names[0]));

    // Set up fake entry
    fake_entry.name = entry_names[dir->entry_index];
    fake_entry.is_directory = (entry_names[dir->entry_index][str_length(entry_names[dir->entry_index]) - 1] == '/');

    dir->entry_index++;

    return &fake_entry;
}

void dir_close(dir_t* dir) {
    if (dir) {
        kfree_tracked(dir);
    }
}

int dir_create(const char* path) {
    // Simplified - in reality would need mkdir syscall
    return -KERNEL_ERROR_NOT_IMPLEMENTED;
}

int dir_remove(const char* path) {
    // Simplified - in reality would need rmdir syscall
    return -KERNEL_ERROR_NOT_IMPLEMENTED;
}

// ============================================================================
// PATH UTILITIES
// ============================================================================

char* path_join(const char* base, const char* relative) {
    if (!base || !relative) return NULL;

    size_t base_len = str_length(base);
    size_t rel_len = str_length(relative);
    size_t total_len = base_len + 1 + rel_len + 1; // / + null terminator

    char* result = kmalloc_tracked(total_len, "path_join");
    if (!result) return NULL;

    str_copy(result, base, total_len);

    // Add separator if needed
    if (base_len > 0 && base[base_len - 1] != '/' && relative[0] != '/') {
        str_copy(result + base_len, "/", 2);
        base_len++;
    }

    str_copy(result + base_len, relative, total_len - base_len);

    return result;
}

char* path_dirname(const char* path) {
    if (!path) return NULL;

    size_t len = str_length(path);
    if (len == 0) return str_duplicate(".");

    // Find last separator
    const char* last_sep = NULL;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_sep = path + i;
        }
    }

    if (!last_sep) {
        return str_duplicate(".");
    }

    size_t dirname_len = last_sep - path;
    if (dirname_len == 0) {
        return str_duplicate("/");
    }

    char* result = kmalloc_tracked(dirname_len + 1, "path_dirname");
    if (!result) return NULL;

    str_copy(result, path, dirname_len + 1);
    result[dirname_len] = '\0';

    return result;
}

char* path_basename(const char* path) {
    if (!path) return NULL;

    size_t len = str_length(path);
    if (len == 0) return str_duplicate("");

    // Find last separator
    const char* last_sep = NULL;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_sep = path + i;
        }
    }

    if (!last_sep) {
        return str_duplicate(path);
    }

    return str_duplicate(last_sep + 1);
}

bool path_is_absolute(const char* path) {
    return path && path[0] == '/';
}
