#pragma once

#define AOS_KERNEL_LOC ((void*)0x100000)
#define AOS_KERNEL_ADDR 0x100000
#define AOS_KERNEL_STACK_TOP 0x208000
#define AOS_KERNEL_SIZE 0x80000 // 512 KB (Placeholder)

#define AOS_BOOT_INFO_ADDR 0x8FF0
#define AOS_BOOT_INFO_LOC ((volatile uint8_t*)AOS_BOOT_INFO_ADDR)

#define AOS_VBA_INFO_ADDR 0x8000

#define AOS_SYSINFO_LBA 4
#define AOS_SYSINFO_SPAN 1 // block span
#define AOS_SYSINFO_SIZE 512 // bytes

#pragma pack(push, 1)
typedef struct {
    uint8_t boot_drive; // BIOS DL Value
    uint8_t boot_mode; // 0=Normal | 1=Recovery | 2=Shell | 3=VGA | 4=VGA+Shell
    uint16_t reserved0; // alignment
    uint64_t total_memory_kib; // Total memory in KiB (RAM)
    uint32_t cpu_signature; // CPUID EAX from 0x1
    char cpu_vendor[13]; // Cpu Vendor
    uint8_t apic_present; // 1 if APIC was found
    uint64_t tsc_freq_hz; // timing info
    uint8_t checksum; // additive checksum
    
    uint8_t reserved[512 - 48]; // Padded to 512 bytes
} aos_sysinfo_t;
#pragma pack(pop)

