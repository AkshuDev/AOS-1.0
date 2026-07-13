// UniBoot - Universal Boot Protocol

#pragma once
#include <inttypes.h>

#define UNIBOOT_MAGIC "UNIBOOT"
#define UNIBOOT_SUBMAGIC_BOOT_INFO "BINFO\x0\x0"
#define UNIBOOT_SUBMAGIC_SYSTEM_MEM_MAP "SMMAP\x0\x0"

#define UNIBOOT_MAGIC_SIZE (sizeof(char) * 8)

#define UNIBOOT_CVERSION 0x010000 // 1.0.0
#define UNIBOOT_CREVISION 1

#define UNIBOOT_BITMASK_FEATURES_X86F_SSE (1 << 0)
#define UNIBOOT_BITMASK_FEATURES_X86F_AVX (1 << 1)

#define UNIBOOT_BITMASK_FEATURES_PAGING (1 << 10)
#define UNIBOOT_BITMASK_FEATURES_64BIT (1 << 11)
#define UNIBOOT_BITMASK_FEATURES_KERNEL_SPACE_PRESENT (1 << 12)
#define UNIBOOT_BITMASK_FEATURES_EXTENSIONS_PRESENT (1 << 13)
#define UNIBOOT_BITMASK_FEATURES_FRAMEBUFFER_PRESENT (1 << 14)

typedef unsigned char uniboot_bool;

#define UNIBOOT_TRUE 1
#define UNIBOOT_FALSE 0

typedef enum {
	UNIBOOT_VENDOR_CLASSIFIED = 0x0,
	UNIBOOT_VENDOR_PHEONIX_STUDIOS = 0x0F33
} uniboot_vendors;

typedef enum {
	UNIBOOT_ARCHITECTURE_UNKNOWN = 0x0,
	
	UNIBOOT_ARCHITECTURE_x86_16 = 0x1,
	UNIBOOT_ARCHITECTURE_x86,
	UNIBOOT_ARCHITECTURE_x64,

	UNIBOOT_ARCHITECTURE_PVCPU = 0x10,

	UNIBOOT_ARCHITECTURE_ARM32 = 0x20,
	UNIBOOT_ARCHITECTURE_ARM64,
	
	UNIBOOT_ARCHITECTURE_RISCV32 = 0x30,
	UNIBOOT_ARCHITECTURE_RISCV64
} uniboot_architecture;

typedef enum {
	UNIBOOT_FB_MODE_NONE = 0x0,
	
	UNIBOOT_FB_MODE_UEFI_GOP = 0x1,
	UNIBOOT_FB_MODE_VGA
} uniboot_fb_mode;

typedef enum {
	UNIBOOT_CFORMAT_RGBA = 0,
    UNIBOOT_CFORMAT_BGRA,
    UNIBOOT_CFORMAT_ABGR,
    UNIBOOT_CFORMAT_ARGB,
    UNIBOOT_CFORMAT_RGB,
    UNIBOOT_CFORMAT_BGR,
} uniboot_cformat;

typedef enum {
	UNIBOOT_BOOT_MODE_NORMAL = 0,
	UNIBOOT_BOOT_MODE_RECOVERY,
	UNIBOOT_BOOT_MODE_PANIC
} uniboot_boot_mode;

typedef enum {
	UNIBOOT_SMMAP_TYPE_UNUSABLE = 0,
	UNIBOOT_SMMAP_TYPE_RESERVED,
	UNIBOOT_SMMAP_TYPE_ACPI_NVS,
	UNIBOOT_SMMAP_TYPE_ACPI_RECLAIM,
	UNIBOOT_SMMAP_TYPE_FREE
} uniboot_smmap_type;

typedef struct uniboot_hdr {
	char magic[UNIBOOT_MAGIC_SIZE];
	char submagic[UNIBOOT_MAGIC_SIZE];

	uint64_t version;
	uint64_t revision;

	uniboot_vendors vendor;
	uniboot_architecture architecture;

	struct uniboot_hdr* next;
	struct uniboot_hdr* root;
	struct uniboot_hdr* prev;
} uniboot_hdr;

typedef struct {
	uint16_t bus, slot, func;
} uniboot_pcie;

typedef struct uniboot_bitmask {
	uint64_t bitmask;
	struct uniboot_bitmask* next;
	struct uniboot_bitmask* root;
	struct uniboot_bitmask* prev;
} uniboot_bitmask;

typedef struct {
	char model[32];
	char vendor[32];
	uint64_t timer_freq;
} uniboot_cpu_info;

typedef struct {
	uniboot_fb_mode mode;
	uint64_t addr;
	uint64_t phys_addr;

	uint64_t size, pitch, width, height;
	uint8_t bpp;

	uniboot_cformat color_format;
} uniboot_fb_info;

typedef struct {
	uniboot_hdr hdr;
	uniboot_bitmask provided_features;
	
	uniboot_pcie boot_drive;
	uniboot_cpu_info cpu_info;
	uniboot_fb_info fb_info;
	uniboot_boot_mode boot_mode;

	uniboot_bool kflag;

	uint64_t checksum;
} uniboot_boot_info;

typedef struct {
	uniboot_smmap_type type;
	uint64_t phys_start;
	uint64_t virt_start;
	uint64_t size;
} uniboot_smmap_entry;

typedef struct {
	uniboot_hdr hdr;
	
	uint64_t count;
	uniboot_smmap_entry* entries;
} uniboot_smmap;