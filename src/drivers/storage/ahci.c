#include "kernel.h"
#include "drivers/block.h"
#include "io.h"

// AHCI / SATA Driver
// Implements basic read/write support for AHCI controllers

// PCI Class/Subclass
#define PCI_CLASS_STORAGE    0x01
#define PCI_SUBCLASS_SATA    0x06
#define PCI_PROG_IF_AHCI     0x01

// AHCI Memory Registers
#define HBA_PORT_IPM_ACTIVE  1
#define HBA_PORT_DET_PRESENT 3

// Port Command and Status
#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000

// HBA Memory Structure
typedef volatile struct tagHBA_PORT {
    uint32_t clb;       // 0x00, command list base address, 1K-byte aligned
    uint32_t clbu;      // 0x04, command list base address upper 32 bits
    uint32_t fb;        // 0x08, FIS base address, 256-byte aligned
    uint32_t fbu;       // 0x0C, FIS base address upper 32 bits
    uint32_t is;        // 0x10, interrupt status
    uint32_t ie;        // 0x14, interrupt enable
    uint32_t cmd;       // 0x18, command and status
    uint32_t rsv0;      // 0x1C, Reserved
    uint32_t tfd;       // 0x20, task file data
    uint32_t sig;       // 0x24, signature
    uint32_t ssts;      // 0x28, SATA status (SCR0:SStatus)
    uint32_t sctl;      // 0x2C, SATA control (SCR2:SControl)
    uint32_t serr;      // 0x30, SATA error (SCR1:SError)
    uint32_t sact;      // 0x34, SATA active (SCR3:SActive)
    uint32_t ci;        // 0x38, command issue
    uint32_t sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t fbs;       // 0x40, FIS-based switch control
    uint32_t rsv1[11];  // 0x44 ~ 0x6F, Reserved
    uint32_t vendor[4]; // 0x70 ~ 0x7F, vendor specific
} HBA_PORT;

typedef volatile struct tagHBA_MEM {
    uint32_t cap;       // 0x00, Host capability
    uint32_t ghc;       // 0x04, Global host control
    uint32_t is;        // 0x08, Interrupt status
    uint32_t pi;        // 0x0C, Ports implemented
    uint32_t vs;        // 0x10, Version
    uint32_t ccc_ctl;   // 0x14, Command completion coalescing control
    uint32_t ccc_pts;   // 0x18, Command completion coalescing ports
    uint32_t em_loc;    // 0x1C, Enclosure management location
    uint32_t em_ctl;    // 0x20, Enclosure management control
    uint32_t cap2;      // 0x24, Host capabilities extended
    uint32_t bohc;      // 0x28, BIOS/OS handoff control and status
    uint8_t  rsv[0xA0-0x2C]; // 0x2C - 0x9F, Reserved
    uint8_t  vendor[0x100-0xA0]; // 0xA0 - 0xFF, Vendor specific
    HBA_PORT ports[1];  // 0x100 - 0x10FF, Port control registers
} HBA_MEM;

// FIS Types
typedef enum {
    FIS_TYPE_REG_H2D   = 0x27, // Register FIS - Host to Device
    FIS_TYPE_REG_D2H   = 0x34, // Register FIS - Device to Host
    FIS_TYPE_DMA_ACT   = 0x39, // DMA Activate FIS - Device to Host
    FIS_TYPE_DMA_SETUP = 0x41, // DMA Setup FIS - Bidirectional
    FIS_TYPE_DATA      = 0x46, // Data FIS - Bidirectional
    FIS_TYPE_BIST      = 0x58, // BIST Activate FIS - Bidirectional
    FIS_TYPE_PIO_SETUP = 0x5F, // PIO Setup FIS - Device to Host
    FIS_TYPE_DEV_BITS  = 0xA1, // Set Device Bits FIS - Device to Host
} FIS_TYPE;

// Register H2D FIS structure
typedef struct tagFIS_REG_H2D {
    uint8_t  fis_type;  // FIS_TYPE_REG_H2D
    uint8_t  pmport:4;  // Port multiplier
    uint8_t  rsv0:3;    // Reserved
    uint8_t  c:1;       // 1: Command, 0: Control
    uint8_t  command;   // Command register
    uint8_t  featurel;  // Feature register, 7:0
    uint8_t  lba0;      // LBA low register, 7:0
    uint8_t  lba1;      // LBA mid register, 15:8
    uint8_t  lba2;      // LBA high register, 23:16
    uint8_t  device;    // Device register
    uint8_t  lba3;      // LBA register, 31:24
    uint8_t  lba4;      // LBA register, 39:32
    uint8_t  lba5;      // LBA register, 47:40
    uint8_t  featureh;  // Feature register, 15:8
    uint8_t  countl;    // Count register, 7:0
    uint8_t  counth;    // Count register, 15:8
    uint8_t  icc;       // Isochronous command completion
    uint8_t  control;   // Control register
    uint8_t  rsv1[4];   // Reserved
} FIS_REG_H2D;

// Command Header
typedef struct tagHBA_CMD_HEADER {
    uint8_t  cfl:5;     // Command FIS length in DWORDS, 2 ~ 16
    uint8_t  a:1;       // ATAPI
    uint8_t  w:1;       // Write, 1: H2D, 0: D2H
    uint8_t  p:1;       // Prefetchable
    uint8_t  r:1;       // Reset
    uint8_t  b:1;       // BIST
    uint8_t  c:1;       // Clear busy upon R_OK
    uint8_t  rsv0:1;    // Reserved
    uint8_t  pmp:4;     // Port multiplier port
    uint16_t prdtl;     // Physical region descriptor table length in entries
    volatile uint32_t prdbc; // Physical region descriptor byte count transferred
    uint32_t ctba;      // Command table descriptor base address
    uint32_t ctbau;     // Command table descriptor base address upper 32 bits
    uint32_t rsv1[4];   // Reserved
} HBA_CMD_HEADER;

// Physical Region Descriptor Table Entry
typedef struct tagHBA_PRDT_ENTRY {
    uint32_t dba;       // Data base address
    uint32_t dbau;      // Data base address upper 32 bits
    uint32_t rsv0;      // Reserved
    uint32_t dbc:22;    // Byte count, 4M max
    uint32_t rsv1:9;    // Reserved
    uint32_t i:1;       // Interrupt on completion
} HBA_PRDT_ENTRY;

// Command Table
typedef struct tagHBA_CMD_TBL {
    uint8_t  cfis[64];  // Command FIS
    uint8_t  acmd[16];  // ATAPI command, 12 or 16 bytes
    uint8_t  rsv[48];   // Reserved
    HBA_PRDT_ENTRY prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TBL;

// Global HBA memory pointer
static HBA_MEM* ahci_hba_mem = NULL;
static block_device_t ahci_devices[32];
static int ahci_device_count = 0;

// Forward declarations
static int ahci_read_port(HBA_PORT* port, uint64_t sector, uint32_t count, void* buffer);
static int ahci_write_port(HBA_PORT* port, uint64_t sector, uint32_t count, const void* buffer);

// Block device wrappers
static int ahci_block_read(block_device_t* dev, uint64_t sector, uint32_t count, void* buffer) {
    HBA_PORT* port = (HBA_PORT*)dev->private_data;
    return ahci_read_port(port, sector, count, buffer);
}

static int ahci_block_write(block_device_t* dev, uint64_t sector, uint32_t count, const void* buffer) {
    HBA_PORT* port = (HBA_PORT*)dev->private_data;
    return ahci_write_port(port, sector, count, buffer);
}

// Find a free command slot
static int find_cmdslot(HBA_PORT* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) return i;
        slots >>= 1;
    }
    return -1;
}

// Read from AHCI port
static int ahci_read_port(HBA_PORT* port, uint64_t sector, uint32_t count, void* buffer) {
    port->is = (uint32_t)-1; // Clear interrupt status
    int spin = 0;
    int slot = find_cmdslot(port);
    if (slot == -1) return -1;

    uint64_t addr = (uint64_t)port->clbu << 32 | port->clb;
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(uintptr_t)addr;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdheader->w = 0; // Read
    cmdheader->prdtl = (uint16_t)((count - 1) >> 4) + 1; // PRDT entries count

    uint64_t tbl_addr = (uint64_t)cmdheader->ctbau << 32 | cmdheader->ctba;
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(uintptr_t)tbl_addr;
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

    // Setup PRDT entries (simplified for contiguous buffer)
    // Note: In a real OS, we'd handle scatter-gather if buffer crosses page boundaries
    // For now, assuming physically contiguous buffer or identity mapping
    uintptr_t buf_phys = (uintptr_t)buffer; // Need physical address in real paging
    
    // 8K bytes (16 sectors) per PRDT entry
    int i = 0;
    uint32_t sectors_left = count;
    
    // Simplified: just one entry for now, assuming small reads
    cmdtbl->prdt_entry[0].dba = (uint32_t)buf_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1; // 512 bytes per sector
    cmdtbl->prdt_entry[0].i = 1;

    // Setup Command FIS
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = 0x25; // READ DMA EXT
    cmdfis->lba0 = (uint8_t)sector;
    cmdfis->lba1 = (uint8_t)(sector >> 8);
    cmdfis->lba2 = (uint8_t)(sector >> 16);
    cmdfis->device = 1 << 6; // LBA mode
    cmdfis->lba3 = (uint8_t)(sector >> 24);
    cmdfis->lba4 = (uint8_t)(sector >> 32);
    cmdfis->lba5 = (uint8_t)(sector >> 40);
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    // Wait for port to be idle
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
    }
    if (spin == 1000000) {
        KERROR("AHCI: Port hung");
        return -1;
    }

    // Issue command
    port->ci = 1 << slot;

    // Wait for completion
    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { // Task File Error
            KERROR("AHCI: Read disk error");
            return -1;
        }
    }

    return 0;
}

// Write to AHCI port
static int ahci_write_port(HBA_PORT* port, uint64_t sector, uint32_t count, const void* buffer) {
    // Similar to read, but set w=1 and command=0x35 (WRITE DMA EXT)
    // Implementation omitted for brevity in this step, focusing on read first
    // But we need it for "write" command
    
    port->is = (uint32_t)-1;
    int spin = 0;
    int slot = find_cmdslot(port);
    if (slot == -1) return -1;

    uint64_t addr = (uint64_t)port->clbu << 32 | port->clb;
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(uintptr_t)addr;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdheader->w = 1; // Write
    cmdheader->prdtl = 1; // Simplified

    uint64_t tbl_addr = (uint64_t)cmdheader->ctbau << 32 | cmdheader->ctba;
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(uintptr_t)tbl_addr;
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + sizeof(HBA_PRDT_ENTRY));

    uintptr_t buf_phys = (uintptr_t)buffer;
    cmdtbl->prdt_entry[0].dba = (uint32_t)buf_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtbl->prdt_entry[0].i = 1;

    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = 0x35; // WRITE DMA EXT
    cmdfis->lba0 = (uint8_t)sector;
    cmdfis->lba1 = (uint8_t)(sector >> 8);
    cmdfis->lba2 = (uint8_t)(sector >> 16);
    cmdfis->device = 1 << 6;
    cmdfis->lba3 = (uint8_t)(sector >> 24);
    cmdfis->lba4 = (uint8_t)(sector >> 32);
    cmdfis->lba5 = (uint8_t)(sector >> 40);
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return -1;

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) return -1;
    }

    return 0;
}

// Initialize AHCI Controller
void ahci_init(void) {
    KINFO("Initializing AHCI Driver...");

    // 1. Scan PCI for AHCI controller
    // Simplified: Assuming we found it at a specific address or passed via boot info
    // In a real OS, we'd use the PCI driver to find Class 0x01, Subclass 0x06
    // For QEMU/Bochs, it's often at a standard location or we can scan.
    
    // Since we don't have a full PCI driver exposed in headers yet, we'll assume
    // a standard QEMU location or scan a few common buses.
    // For this implementation, let's assume we found the BAR5 (ABAR)
    
    // Mocking PCI scan result for QEMU default AHCI
    // In QEMU with -device ahci, the MMIO base is usually allocated by BIOS.
    // We need to read PCI config space.
    
    // Since we lack PCI code in this snippet, I'll implement a minimal PCI config read.
    // Ports: 0xCF8 (Address), 0xCFC (Data)
    
    uint32_t pci_address = 0x80000000; // Bus 0, Dev 0, Func 0
    uint32_t abar = 0;
    int found = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8);
                
                // Read Vendor ID / Device ID
                outl(0xCF8, addr);
                uint32_t id = inl(0xCFC);
                
                if ((id & 0xFFFF) != 0xFFFF) {
                    // Read Class/Subclass (Offset 0x08)
                    outl(0xCF8, addr | 0x08);
                    uint32_t class_code = inl(0xCFC);
                    uint8_t base_class = (class_code >> 24) & 0xFF;
                    uint8_t sub_class = (class_code >> 16) & 0xFF;
                    
                    if (base_class == 0x01 && sub_class == 0x06) {
                        // Found AHCI
                        KINFO("Found AHCI Controller at %d:%d:%d", bus, slot, func);
                        
                        // Read BAR5 (ABAR) - Offset 0x24
                        outl(0xCF8, addr | 0x24);
                        abar = inl(0xCFC);
                        found = 1;
                        goto found_ahci;
                    }
                }
            }
        }
    }

found_ahci:
    if (!found || abar == 0) {
        KWARN("No AHCI Controller found");
        return;
    }

    // Map ABAR (physical) to virtual memory
    // In our kernel, we might need to map it. For now, assuming identity mapping or simple access.
    // ABAR is usually physical.
    ahci_hba_mem = (HBA_MEM*)(uintptr_t)(abar & 0xFFFFFFF0);
    
    // Enable AHCI mode (GHC.AE)
    ahci_hba_mem->ghc |= 0x80000000;
    
    // Scan ports
    uint32_t pi = ahci_hba_mem->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            int dt = ahci_hba_mem->ports[i].ssts & 0x0F;
            int ipm = (ahci_hba_mem->ports[i].ssts >> 8) & 0x0F;
            
            if (dt == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                int type = ahci_hba_mem->ports[i].sig;
                if (type == 0x0101) { // SATA
                    KINFO("SATA Drive found at port %d", i);
                    
                    // Initialize port (allocate memory for CLB/FB)
                    // Simplified: Assuming BIOS set it up or we allocate
                    // We should allocate strictly speaking.
                    
                    // Register block device
                    block_device_t* dev = &ahci_devices[ahci_device_count++];
                    sprintf(dev->name, "sata%d", i);
                    dev->type = BLOCK_DEVICE_TYPE_HARD_DISK;
                    dev->sector_size = 512;
                    dev->total_sectors = 0; // TODO: Identify drive to get size
                    dev->read = ahci_block_read;
                    dev->write = ahci_block_write;
                    dev->private_data = (void*)&ahci_hba_mem->ports[i];
                    
                    block_register_device(dev);
                }
            }
        }
        pi >>= 1;
    }
}
