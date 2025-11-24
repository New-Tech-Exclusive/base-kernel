#include "kernel.h"
#include "drivers/block.h"
#include "vfs.h"

// FAT32 Filesystem Driver
// Implements basic read support for FAT32

// Boot Sector / BPB Structure
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // FAT32 Extended
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved2;
    uint8_t  signature;
    uint32_t vol_id;
    char     vol_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

// Directory Entry Structure
typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed)) fat32_dir_entry_t;

// Attributes
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LONG_NAME 0x0F

// FAT32 Context
typedef struct {
    block_device_t* dev;
    fat32_bpb_t bpb;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
} fat32_fs_t;

// Read a cluster from FAT table
static uint32_t fat32_read_fat(fat32_fs_t* fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t ent_offset = fat_offset % fs->bpb.bytes_per_sector;
    
    uint8_t buffer[512]; // Assuming 512 byte sectors
    fs->dev->read(fs->dev, fat_sector, 1, buffer);
    
    uint32_t table_value = *(uint32_t*)&buffer[ent_offset];
    return table_value & 0x0FFFFFFF;
}

// Convert cluster to sector
static uint32_t fat32_cluster_to_sector(fat32_fs_t* fs, uint32_t cluster) {
    return fs->data_start_sector + ((cluster - 2) * fs->bpb.sectors_per_cluster);
}

// Read file content
int fat32_read_file(fat32_fs_t* fs, uint32_t start_cluster, void* buffer, uint32_t size) {
    uint32_t cluster = start_cluster;
    uint32_t bytes_read = 0;
    uint8_t* buf = (uint8_t*)buffer;
    uint32_t cluster_size = fs->bpb.sectors_per_cluster * fs->bpb.bytes_per_sector;
    
    while (bytes_read < size) {
        if (cluster >= 0x0FFFFFF8) break; // End of chain
        
        uint32_t sector = fat32_cluster_to_sector(fs, cluster);
        uint32_t to_read = size - bytes_read;
        if (to_read > cluster_size) to_read = cluster_size;
        
        // Read full cluster (simplified, should handle partial sectors)
        // For now, assuming buffer is large enough and aligned
        fs->dev->read(fs->dev, sector, fs->bpb.sectors_per_cluster, buf + bytes_read);
        
        bytes_read += to_read; // Assuming we read full cluster or up to size
        cluster = fat32_read_fat(fs, cluster);
    }
    
    return bytes_read;
}

// List directory
void fat32_ls(fat32_fs_t* fs, uint32_t dir_cluster) {
    uint32_t cluster = dir_cluster;
    uint8_t buffer[4096]; // Cluster size usually 4K
    
    KINFO("Listing directory (Cluster %d):", dir_cluster);
    
    while (cluster < 0x0FFFFFF8) {
        uint32_t sector = fat32_cluster_to_sector(fs, cluster);
        fs->dev->read(fs->dev, sector, fs->bpb.sectors_per_cluster, buffer);
        
        fat32_dir_entry_t* entry = (fat32_dir_entry_t*)buffer;
        for (int i = 0; i < 128; i++) { // Assuming 4K cluster / 32 byte entry
            if (entry[i].name[0] == 0x00) return; // End of directory
            if (entry[i].name[0] == 0xE5) continue; // Deleted
            
            if (entry[i].attr == ATTR_LONG_NAME) continue; // Skip LFN for now
            
            char name[12];
            memcpy(name, entry[i].name, 11);
            name[11] = 0;
            
            // Format name (remove spaces)
            // ... (omitted for brevity)
            
            KINFO("  %s  %s  %d bytes", name, 
                  (entry[i].attr & ATTR_DIRECTORY) ? "<DIR>" : "",
                  entry[i].size);
        }
        
        cluster = fat32_read_fat(fs, cluster);
    }
}

// Initialize FAT32 on a block device
fat32_fs_t* fat32_init(block_device_t* dev) {
    fat32_fs_t* fs = kmalloc_tracked(sizeof(fat32_fs_t), "fat32_fs");
    if (!fs) return NULL;
    
    fs->dev = dev;
    
    // Read BPB (Sector 0)
    uint8_t buffer[512];
    dev->read(dev, 0, 1, buffer);
    memcpy(&fs->bpb, buffer, sizeof(fat32_bpb_t));
    
    // Verify signature
    if (fs->bpb.signature != 0x29 && fs->bpb.signature != 0x28) {
        // Check boot signature at 510
        if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
            KERROR("FAT32: Invalid boot signature");
            kfree_tracked(fs);
            return NULL;
        }
    }
    
    // Calculate offsets
    fs->fat_start_sector = fs->bpb.reserved_sectors;
    fs->data_start_sector = fs->bpb.reserved_sectors + (fs->bpb.fats * fs->bpb.sectors_per_fat_32);
    
    KINFO("FAT32 Initialized on %s", dev->name);
    KINFO("  Volume Label: %.11s", fs->bpb.vol_label);
    KINFO("  Root Cluster: %d", fs->bpb.root_cluster);
    
    return fs;
}

// Global FS instance for demo
fat32_fs_t* root_fs = NULL;

// Mount sata0 as root
void fat32_mount_root(void) {
    block_device_t* dev = block_get_device("sata0");
    if (dev) {
        root_fs = fat32_init(dev);
        if (root_fs) {
            KINFO("Mounted sata0 as root filesystem");
        } else {
            KERROR("Failed to mount sata0");
        }
    } else {
        KWARN("sata0 device not found (AHCI init failed?)");
    }
}

// CLI Commands
void cmd_ls(const char* args) {
    if (!root_fs) {
        KERROR("No filesystem mounted");
        return;
    }
    fat32_ls(root_fs, root_fs->bpb.root_cluster);
}

void cmd_cat(const char* args) {
    // Simplified: just read a specific file if implemented lookup
    KINFO("cat not fully implemented yet (needs path parsing)");
}
