#pragma once

#include <inttypes.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/io/drive.h>

#define AMBRC_MAGIC 0x0A130B180C000000 // It litrally says AMBRC\0\0\0 (but not in string) (13=M,18=B)
#define CURRENT_AMBRC_VERSION 0x0000000000010000 // 0.0.1.0

#define CURRENT_AMBRC_VERSION_STR "1.0"

#define AMBRC_MAX_KERNELS 12

struct ambrc_display {
    enum VMemColors bg_color;
    enum VMemColors fg_color;
    enum VMemColors selected_bg_color;
    enum VMemColors selected_fg_color;

    enum VMemColors ambrc_bg_color;
    enum VMemColors ambrc_fg_color;
    enum VMemColors ambrc_selected_bg_color;
    enum VMemColors ambrc_selected_fg_color;

    enum VMemColors error_fg_color;
} __attribute__((packed));

struct ambrc_boot_info {
    uint16_t default_os_idx; // value+1 = idx,0=not present
    uint16_t safe_os_idx; // value+1 = idx,0=not present
    uint16_t panic_os_idx; // value+1 = idx,0=not present

    uint8_t safe_mode_flags; // 0=empty
} __attribute__((packed));

struct ambrc_kernel_info {
    uint64_t load_addr;
    uint64_t entry_point;

    uint8_t safe_mode_flags;
} __attribute__((packed));

struct ambrc {
    // Core
    uint64_t magic;
    uint64_t version;
    // Display
    struct ambrc_display display;
    // Boot info
    struct ambrc_boot_info boot_info;
    // Kernel info
    struct ambrc_kernel_info kernel_info[AMBRC_MAX_KERNELS];
    // Checksum
    uint32_t crc32;

    // Reserved
    char res[757]
} __attribute__((packed));

extern struct ambrc backup_ambrc;

void init_backup_ambrc(void) __attribute__((used));
void start_ambrc(struct drive_device* drive) __attribute__((used));
struct ambrc* get_ambrc(void) __attribute__((used));