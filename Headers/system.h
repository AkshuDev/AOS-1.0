#pragma once

#include <inttypes.h>

#define AOS_KERNEL_LOC ((void *)0x100000)
#define AOS_KERNEL_ADDR 0x100000
#define AOS_KERNEL_STACK_TOP 0x208000
#define AOS_KERNEL_SIZE 0x80000 // 512 KB (Placeholder)

#define AOS_BOOT_INFO_ADDR 0x7FF0
#define AOS_BOOT_INFO_LOC ((volatile uint8_t*)AOS_BOOT_INFO_ADDR)

#define AOS_E820_INFO_ADDR 0x8000
#define AOS_E820_INFO_LOC ((volatile uint8_t*)AOS_EBX_INFO_ADDR)

#define AOS_SYSINFO_LBA 4
#define AOS_SYSINFO_SPAN 1   // block span
#define AOS_SYSINFO_SIZE 512 // bytes

#define AOS_USER_SPACE_BASE 0x0000000000000000ULL
#define AOS_DIRECT_MAP_BASE 0xFFFF800000000000ULL
#define AOS_KERNEL_SPACE_BASE 0xFFFFA00000000000ULL
#define AOS_DRIVER_SPACE_BASE 0xFFFFC00000000000ULL
#define AOS_SENSITIVE_SPACE_BASE 0xFFFFE00000000000ULL

#pragma pack(push, 1)
typedef struct {
  uint8_t boot_drive; // BIOS DL Value
  uint8_t boot_mode;  // 0=Normal | 1=Recovery | 2=Shell | 3=VGA | 4=VGA+Shell
  uint16_t reserved0; // alignment
  uint32_t cpu_signature;    // CPUID EAX from 0x1
  char cpu_vendor[13];       // Cpu Vendor
  uint8_t apic_present;      // 1 if APIC was found
  uint64_t tsc_freq_hz;      // timing info
  uint8_t checksum;          // additive checksum

  uint8_t reserved[512 - 48]; // Padded to 512 bytes
} aos_sysinfo_t;
#pragma pack(pop)

#define AOS_SYS_INFO_ADDR 0x2000
#define AOS_SYS_INFO_LOC ((volatile aos_sysinfo_t*)AOS_SYS_INFO_ADDR)

