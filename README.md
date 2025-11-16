# Base Kernel

A monolithic operating system kernel for x86-64 desktops, inspired by Linux but implemented as a clean-room design.

## Architecture

- **Design**: Monolithic kernel
- **Architecture**: x86-64
- **Target Platform**: Consumer desktop computers
- **License**: MIT (clean-room implementation)

## Build Requirements

- Cross-compiler (GCC targeting x86-64-elf)
- NASM assembler
- Make
- GRUB bootloader
- QEMU for testing

## Development Environment Setup (WSL/Ubuntu)

```bash
# Install required packages
sudo apt update
sudo apt install build-essential nasm grub-pc-bin grub-common xorriso qemu-system-x86 gcc-multilib g++-multilib

# Cross-compiler setup (if needed)
# Build gcc cross-compiler targeting x86_64-elf
```

## Kernel Structure

```
src/
├── arch/
│   └── x86_64/
│       ├── boot/
│       ├── gdt/
│       ├── idt/
│       ├── paging/
│       └── ...
├── kernel/
│   ├── memory/
│   ├── scheduler/
│   ├── syscall/
│   └── ...
├── drivers/
│   ├── video/
│   ├── serial/
│   └── ...
├── include/
│   ├── kernel.h
│   └── ...
└── kernel.ld
```

## Building

```bash
make all
make run    # Run in QEMU
```

## Features Implemented

- [ ] Boot process with GRUB multiboot
- [ ] GDT setup
- [ ] IDT and ISR handlers
- [ ] Basic paging
- [ ] Physical memory management
- [ ] Kernel heap allocation
- [ ] Serial driver for debugging
- [ ] VGA text mode driver

## License

MIT License (see LICENSE file)
