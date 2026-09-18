# AOS++
![Version](https://img.shields.io/badge/Version-1.0-red?style=for-the-badge)
![License](https://img.shields.io/badge/License-GNU%20GPL%20v3.0-red?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-In%20Development-red?style=for-the-badge)

AOS++ is a fully independent operating system built from scratch with zero external runtime dependencies, powered entirely by the Pheonix Ecosystem.
It includes a custom bootloader, kernel (AOS), filesystem (PBFS), userland foundation, and more.

AOS stands for Aftergreat Operating System, it is the core Kernel of the OS.

When referring AOS: It means the kernel driving this Operating System, however when referring AOS++: It means the Operating System itself.

# How to begin
## Cloning
To start off, you would need to clone the repository to your computer. To do so, you can use *git* or directly download the code from *github*, and then go into that directory/folder.

### Git
To clone this repository via *git*, run this *git* command:
```shell
git clone https://github.com/AkshuDev/AOS-1.0
```

### Github
To clone this repository via *github*, use the download option in github

## Build Dependencies
This repository requires many tools for building AOS and other parts. You would need to download all the required tools -
1. *gcc* (*GNU Compiler Collection*) - Required as the C Compiler
2. *make* (*GNU Make*) - Required to automate build process
3. *GNU binutils* - Required for multiple tasks
4. *MingW W64* - Required for building the *UEFI* version of AOS Bootloader
5. *mtools* (*GNU Mtools*) - Required for building the *UEFI* version of AOS Bootloader
6. *bash* (*Bourne Again SHell*) - Required for the use of running helper *.sh* files in this repository
7. *qemu* (*Quick Emulator*) - Needed for testing via helper *.sh* files. Not required, but recommended

## Setup
Before building the project, you would need to do a small setup, thankfully this setup is automated by the [init.sh](init.sh) file.
```bash
./init.sh
```

This file will clone all required submodules ([See 'Submodules' Section](#Submodules)), as well as setup other things such as Environment Variables for that session.

## Building
To build this repository, you first need to clean off the junk by using *make* -
```bash
make clean \
make -C Kernel clean \
make -C Bootloader clean
```

Then you can build the actual OS by using *make*.

For *MBR* Support -
```bash
make build_mbr
```

For *UEFI* Support -
```bash
make build_uefi
```

## Testing and Running
To test/run the OS, you can use the [run.sh](run.sh) helper file -
```bash
./run.sh
```

You can add flags for various configurations of the virtual machine, use *--help* or *-h* for more info -
```bash
./run.sh --help
```

Example Configuration -
```bash
./run.sh -he -cpu=intel -gpu=virtio -adev=nvme -uefi
```
This example configuration, creates a session with an *Intel* High-End CPU, *VirtIO* GPU, *UEFI* Firmware, and an *NVMe* alongside default boot device *SATA* (Can be changed via *-bdev* flag)

# Drivers and Support
## GPU (Graphics Processing Unit)
AOS has inbuilt GPU Drivers of the following types/vendors -
1. *VirtIO*

Planned support for multiple vendors such as - *AMD*, *Nvidia*, *Intel*, etc

## Serial
AOS has inbuilt Serial Drivers for COM1/COM2 Ports via I/O, hence this is mostly accessible via the correct Chipset and device, however it will not cause any issues if the device is not present

## USB (Universal Serial Bus)
AOS has the following USB Drivers -
1. *xHCI* (Modern)

Legacy Compatibility planned for future implementation

## Disks
AOS has the following Disk/Drive Drivers -
1. *SATA*
2. *ATA*

*NVMe*, *HDD* support planned for implementation in the future progress of AOS

## ACPI
AOS supports *Legacy ACPI* and *ACPI 2.0*

## Graphics Libraries
AOS has inbuilt support for *Pyrion* (Created by *AkshuDev/Pheonix Studios*)

*OpenGL*, *Vulkan* support will be implemented soon

# Features
AOS has multiple features -
1. Multi-Core
2. Multi-Thread
3. PCIe and PCI support
4. MMIO and IO support
5. Various inbuilt universal drivers
6. No external dependencies except the ones from Pheonix Ecosystem (Created by *AkshuDev/Pheonix Studios*)

And more. Please check out the code for extra information!

# AOS Bootloader
AOS Bootloader is an inbuilt bootloader which allows Multi-OS Loading via PBFS's Kernel Table feature. Hence AOS Bootloader can only run on *PBFS* formatted disks.

## Supported Firmware Interfaces
AOS Bootloader supports -
1. *UEFI*
2. *MBR* (***BIOS***)

However in the coming future, it will also keep support for *PFI (**Pheonix Firmware Interface**)*

# Architecture Compatibility
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

# Site
Please visit the [AOS++](https://pheonix-studios-git.github.io/Pheonix-Studios/pages/os.html) page on [Pheonix Studios](https://pheonix-studios-git.github.io/Pheonix-Studios) website!

# License
This project is under *GNU General Public License V3.0*, see the [License](LICENSE) file for more information.
