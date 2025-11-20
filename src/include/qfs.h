/*
 * Quantum Filesystem Header
 * Public API for QFS
 */

#ifndef QFS_H
#define QFS_H

#include "types.h"

// ============================================================================
// CONSTANTS
// ============================================================================

#define QFS_MAGIC 0x51465321  // "QFS!"

// ============================================================================
// TYPES
// ============================================================================

typedef struct qfs_inode qfs_inode_t;
typedef struct qfs_superblock qfs_superblock_t;
typedef struct qfs_extent qfs_extent_t;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Initialization
int qfs_init(void);

// File operations
uint32_t qfs_create_file(const char* name, uint16_t mode);
int64_t qfs_read(uint32_t ino, void* buffer, uint64_t offset, size_t count);
int64_t qfs_write(uint32_t ino, const void* buffer, uint64_t offset, size_t count);

// Statistics
void qfs_get_stats(void);

// Demo functions (for compatibility with existing code)
void fluxfs_quantum_position_demo(uint64_t inode_num, uint64_t size);
void fluxfs_temporal_demo(void);
void fluxfs_adaptive_raid_demo(void);

#endif /* QFS_H */
