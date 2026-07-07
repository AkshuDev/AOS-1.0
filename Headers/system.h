#pragma once

#include <aos_inttypes.h>
#include <inc/drivers/core/framebuffer.h>

#define AOS_KERNEL_LOC ((void *)0x100000)
#define AOS_KERNEL_ADDR 0x100000
#define AOS_KERNEL_STACK_TOP 0x208000
#define AOS_KERNEL_SIZE 0x80000 // 512 KB (Placeholder)

#define AOS_BOOT_INFO_ADDR 0x7FF0
#define AOS_BOOT_INFO_LOC ((volatile uint8_t*)AOS_BOOT_INFO_ADDR)

#define AOS_E820_INFO_ADDR 0x8000
#define AOS_E820_INFO_LOC ((volatile uint8_t*)AOS_E820_INFO_ADDR)

#define AOS_SYSINFO_LBA 4
#define AOS_SYSINFO_SPAN 1   // block span
#define AOS_SYSINFO_SIZE 512 // bytes

#define AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG (1 << 1)

#ifndef AOS_USER_SPACE_BASE
	#define AOS_USER_SPACE_BASE 0x0000000010000000ULL
#endif
#ifndef AOS_DIRECT_MAP_BASE
	#define AOS_DIRECT_MAP_BASE 0xFFFF800000000000ULL
#endif
#ifndef AOS_KERNEL_SPACE_BASE
	#define AOS_KERNEL_SPACE_BASE 0xFFFFA00000000000ULL
#endif
#ifndef AOS_DRIVER_SPACE_BASE
	#define AOS_DRIVER_SPACE_BASE 0xFFFFC00000000000ULL
#endif
#ifndef AOS_SENSITIVE_SPACE_BASE
	#define AOS_SENSITIVE_SPACE_BASE 0xFFFFE00000000000ULL
#endif

#define AOS_SYSINFO_FB_MODE_VGA 0
#define AOS_SYSINFO_FB_MODE_FB 1

struct aos_sysinfo_pcie {
	uint16_t bus;
	uint16_t slot;
	uint16_t func;
};

#pragma pack(push, 1)
typedef struct {
	struct aos_sysinfo_pcie boot_drive;
	uint8_t boot_drive_raw;
	uint8_t boot_mode;  // 0=Normal | 1=Recovery | 2=Shell | 3=VGA | 4=VGA+Shell
	uint16_t reserved0; // alignment
	uint32_t cpu_signature; // CPUID EAX from 0x1
	char cpu_vendor[13]; // Cpu Vendor
	aos_bool apic_present; // 1 if APIC was found
	uint64_t tsc_freq_hz; // timing info
	uint8_t kernel_info; // Kernel information such as, kernel active flag
	FB_Info_t fb_info;
	uint8_t fb_mode;
	uint64_t checksum; // additive checksum
} aos_sysinfo_t;
#pragma pack(pop)

#define AOS_SYS_INFO_ADDR 0x10000
#define AOS_SYS_INFO_LOC ((volatile aos_sysinfo_t*)AOS_SYS_INFO_ADDR)

