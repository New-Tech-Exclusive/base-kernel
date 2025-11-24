#include "drivers/block.h"
#include "kernel.h"

#define MAX_BLOCK_DEVICES 8

static block_device_t* devices[MAX_BLOCK_DEVICES];
static int device_count = 0;

int block_register_device(block_device_t* dev) {
    if (device_count >= MAX_BLOCK_DEVICES) return -1;
    devices[device_count++] = dev;
    KINFO("Registered block device: %s", dev->name);
    return 0;
}

block_device_t* block_get_device(const char* name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }
    return NULL;
}
