# AOS++ and AOS
AOS++ is a fully independent operating system built from scratch with zero external dependencies, powered entirely by the Pheonix Ecosystem.
It includes a custom bootloader, kernel (AOS), filesystem (PBFS), and userland foundation.

AOS stands for Aftergreat Operating System, it is the core Kernel of the OS.

When refering AOS: It means the kernel driving this Operating System, however when referring AOS++: It means the Operating System Itself.

## Drivers
### GPU (Graphics Processing Unit)
AOS has inbuilt GPU Drivers of the following types/vendors -
1. *VirtIO*

Planned support for multiple vendors such as - *AMD*, *Nvidia*, *Intel*, etc

### Serial
AOS has inbuilt Serial Drivers for COM1/COM2 Ports via I/O, hence this is mostly accessable via the correct Chipset and device, however it will not cause any issues if the device is not present

### USB (Universal Serial Bus)
AOS has the following USB Drivers -
1. *xHCI* (Modern)

Legacy Compatibility planned for future implementation

### Disks
AOS has the following Disk/Drive Drivers -
1. *SATA*
2. *ATA*

*NVMe*, *HDD* support planned for implementation in the future progress of AOS

### ACPI
AOS Supports *Legacy ACPI* and *ACPI 2.0*

### Graphics Libraries
AOS has inbuilt support for *Pyrion* (Created by *AkshuDev/Pheonix Studios*)

*OpenGL*, *Vulkan* support will be implemented soon

## Features
AOS has multiple features -
1. Multi-Core
2. Multi-Thread
3. PCIe and PCI support
4. MMIO and IO support
5. Various inbuilt universal drivers
6. No external dependencies except the ones from Pheonix Ecosystem (Created by *AkshuDev/Pheonix Studios*)

And more, please check out the code for more information!

# AOS Bootloader
AOS Bootloader is a inbuilt bootloader which allows Multi-OS Loading via PBFS's Kernel Table feature. Hence AOS Bootloader can only run on *PBFS* formatted disks.

## Supported Firmware Interfaces
AOS Bootloader supports -
1. *UEFI*
2. *MBR* (***BIOS***)

However in the comming future, it will also keep support for *PFI (**Pheonix Firmware Interface**)*

# Compatibility
AOS and AOS Bootloader are compatible with ->
1. *x86*
2. *x64/x86_64*

However it will proceed to increase its compatibility in following future.

# Full workflow
AOS Bootloader -> AOS -> Core Systems (PBFS/SMP/Drivers/ACPI/PCIe/etc) -> Pheonix Ecosystem (Libs/APIs) -> AOS++ (Desktop Environment/Kernel Extensions and more)

# Roadmap

- OpenGL / Vulkan support
- Networking stack (TCP/IP)
- ARM architecture support
- Security model (user/kernel isolation)
- Executable support (.aosf/.casod)
- Graphical desktop environment

# Submodules
## PBFS
*PBFS* or *Pheonix Block File System* is a 128-bit addressable file system used by AOS and AOS Bootloader. It is created by - *AkshuDev/Pheonix Studios*. It has various features such as Kernel Tables, Sysinfo, Bootloader Partitions (Allows easy setup of UEFI or MBR bootloaders without a hassle), and more. Link - https://github.com/AkshuDev/Pheonix-Block-File-System

# License

MIT License

Copyright (c) 2026 AkshuDev

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
