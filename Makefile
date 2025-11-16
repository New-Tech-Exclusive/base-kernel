# Base Kernel Makefile

# Toolchain configuration
CC := gcc
AS := nasm
LD := ld

# Architecture and target
ARCH := x86_64
TARGET := $(ARCH)-elf

# Cross-compiler prefix (uncomment if using cross-compiler)
# CC := $(TARGET)-gcc
# AS := $(TARGET)-as
# LD := $(TARGET)-ld

# Compiler and assembler flags
CFLAGS := -std=c11 -ffreestanding -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra \
          -I src/include -I src/arch/$(ARCH)/include -I src -fno-stack-protector

ASFLAGS := -f elf64

LDFLAGS := -m elf_x86_64 -T src/kernel.ld -nostdlib -z max-page-size=0x1000
OBJCOPY := objcopy

# Create raw binary image for bootloader
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

# Create floppy disk image (1.44MB)
bootdisk.img: kernel.bin
	# Create 1.44MB disk image
	dd if=/dev/zero of=$@ bs=512 count=2880
	# Write bootloader to sector 0
	dd if=src/arch/x86_64/boot/boot.bin of=$@ bs=512 count=1 conv=notrunc
	# Write kernel binary starting at sector 2
	dd if=kernel.bin of=$@ bs=512 seek=2 conv=notrunc

# Create bootloader binary (just the first 512 bytes with boot sector)
src/arch/x86_64/boot/boot.bin: src/arch/x86_64/boot/boot.o
	$(OBJCOPY) -O binary $< $@ && dd if=$@ of=$@ bs=512 count=1

# Source files
BOOT_SRCS := $(wildcard src/arch/$(ARCH)/boot/*.asm) \
             $(wildcard src/arch/$(ARCH)/boot/*.c)

ARCH_SRCS := $(wildcard src/arch/$(ARCH)/*.asm) \
             $(wildcard src/arch/$(ARCH)/*.c) \
             $(wildcard src/arch/$(ARCH)/gdt/*.c) \
             $(wildcard src/arch/$(ARCH)/idt/*.c) \
             $(wildcard src/arch/$(ARCH)/paging/*.c)

KERNEL_SRCS := $(wildcard src/kernel/*.c) \
               $(wildcard src/kernel/memory/*.c) \
               $(wildcard src/kernel/scheduler/*.c) \
               $(wildcard src/kernel/syscall/*.c) \
               $(wildcard src/fs/*.c)

# Exclude stubs.c and scheduler_temp.c since we have real implementations now
KERNEL_SRCS := $(filter-out src/kernel/stubs.c src/kernel/scheduler_temp.c, $(KERNEL_SRCS))

DRIVER_SRCS := $(wildcard src/drivers/*.c) \
               $(wildcard src/drivers/video/*.c) \
               $(wildcard src/drivers/serial/*.c) \
               $(wildcard src/drivers/keyboard/*.c)

SRCS := $(BOOT_SRCS) $(ARCH_SRCS) $(KERNEL_SRCS) $(DRIVER_SRCS)

# Object files
BOOT_OBJS := $(BOOT_SRCS:.asm=.o)
BOOT_OBJS := $(BOOT_OBJS:.c=.o)
ARCH_OBJS := $(ARCH_SRCS:.asm=.o)
ARCH_OBJS := $(ARCH_OBJS:.c=.o)
KERNEL_OBJS := $(KERNEL_SRCS:.c=.o)
DRIVER_OBJS := $(DRIVER_SRCS:.c=.o)

OBJS := $(BOOT_OBJS) $(ARCH_OBJS) $(KERNEL_OBJS) $(filter-out src/drivers/video/vga.o, $(DRIVER_OBJS))

# Build targets
.PHONY: all clean run iso debug

all: base-kernel.iso

iso: base-kernel.iso

# Link kernel binary
kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# Compile C sources
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM sources
%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Create bootable ISO
base-kernel.iso: kernel.elf
	mkdir -p isofiles/boot/grub
	cp kernel.elf isofiles/boot/
	cp scripts/grub.cfg isofiles/boot/grub/
	grub-mkrescue -o $@ isofiles/ 2>/dev/null || grub2-mkrescue -o $@ isofiles/

# Run in QEMU using iso with GRUB
run: base-kernel.iso
	qemu-system-x86_64 -cdrom $< -boot d -m 512M

# Run disk image in QEMU
rundisk: bootdisk.img
	qemu-system-x86_64 -drive file=$<,format=raw -boot a -m 512M -serial stdio -nographic

# Run with GDB debugging
debug: base-kernel.iso
	qemu-system-x86_64 -cdrom $< -boot d -m 512M -s -S &
	gdb -ex "target remote localhost:1234" -ex "symbol-file kernel.elf"

# Clean build artifacts
clean:
	rm -rf $(OBJS) kernel.elf base-kernel.iso isofiles/

# Print build information
info:
	@echo "Architecture: $(ARCH)"
	@echo "Target: $(TARGET)"
	@echo "Sources: $(SRCS)"
	@echo "Objects: $(OBJS)"

# Development helpers
headers:
	find src/ -name "*.h" -exec cp {} src/include/ \;

# Create disassembly for debugging
disasm: kernel.elf
	objdump -d $< > kernel.disasm
	objdump -h $< > kernel.sections
