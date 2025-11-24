#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

typedef enum {
    BLOCK_DEVICE_TYPE_HARD_DISK,
    BLOCK_DEVICE_TYPE_FLOPPY,
    BLOCK_DEVICE_TYPE_CDROM,
    BLOCK_DEVICE_TYPE_RAMDISK
} block_device_type_t;

typedef struct block_device {
    char name[32];
    block_device_type_t type;
    uint64_t sector_size;
    uint64_t total_sectors;
    
    int (*read)(struct block_device* dev, uint64_t sector, uint32_t count, void* buffer);
    int (*write)(struct block_device* dev, uint64_t sector, uint32_t count, const void* buffer);
    
    void* private_data;
} block_device_t;

// Manager functions
int block_register_device(block_device_t* dev);
block_device_t* block_get_device(const char* name);

#endif // BLOCK_H
