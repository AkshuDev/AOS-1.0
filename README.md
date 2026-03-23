# AOS++ and AOS
AOS stands for Aftergreat Operating System, it is the core Kernel of the OS.

The '++' Refers to the abililty of GUI/Package Manager/Desktop Environment/Window System/Kernel Extensions, it is a superset of AOS.

## Drivers
### GPU (Graphics Processing Unit)
AOS has inbuilt GPU Drivers of the following types/vendors -
1. *VirtIO*

AOS is constantly* being updated more more support

### Serial
AOS has inbuilt Serial Drivers for COM1/COM2 Ports via I/O, hence this is mostly accessable via the correct Chipset and device, however it will not cause any issues if the device is not present

### USB (Universal Serial Bus)
AOS has the following USB Drivers -
1. *xHCI* (Modern)

AOS is constantly* being updated more more support

### Disks
AOS has the following Disk/Drive Drivers -
1. *SATA*
2. *ATA*

AOS is constantly* being updated more more support

### ACPI
AOS Supports *Legacy ACPI* and *ACPI 2.0*

### Graphics Libraries
AOS has inbuilt support for *Pyrion* (Created by *AkshuDev/Pheonix Studios* (**ME**)) and *OpenGL* and *Vulkan* Support will be added

## Features
AOS has multiple features -
1. Multi-Core
2. Multi-Thread
3. PCIe and PCI support
4. MMIO and IO support
5. Various inbuilt universal drivers
6. No external dependencies except the ones from Pheonix Ecosystem (Created by *AkshuDev/Pheonix Studios* (**ME**))

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

# Submodules
## PBFS
*PBFS* or *Pheonix Block File System* is a 128-bit addressable file system used by AOS and AOS Bootloader. It is created by - *AkshuDev/Pheonix Studios* (**ME**). It has various features such as Kernel Tables, Sysinfo, Bootloader Partitions (Allows easy setup of UEFI or MBR bootloaders without a hassle), and more. Link - https://github.com/AkshuDev/Pheonix-Block-File-System
