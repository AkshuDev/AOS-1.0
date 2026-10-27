#pragma once
#include <asm.h>
#include <system.h>
#include <uniboot.h>

#include <PBFS/headers/pbfs-fs.h>

//! UINT64_MAX is INVALID in any case for VMem
#define IO_VMEM_INVALID 0xFFFFFFFFFFFFFFFFULL

#define IO_VMEM 0xB8000
#define IO_VMEM_MAX_COLS 80
#define IO_VMEM_MAX_ROWS 25

extern uint64_t IO_VMEM_MAX_COLS_true;
extern uint64_t IO_VMEM_MAX_ROWS_true;

enum VMemColors {
    VMEM_COLOR_BLACK = 0x0,
    VMEM_COLOR_BLUE = 0x1,
    VMEM_COLOR_GREEN = 0x2,
    VMEM_COLOR_CYAN = 0x3,
    VMEM_COLOR_RED = 0x4,
    VMEM_COLOR_MAGENTA = 0x5,
    VMEM_COLOR_BROWN = 0x6,
    VMEM_COLOR_LIGHT_GRAY = 0x7,
    VMEM_COLOR_DARK_GRAY = 0x8,
    VMEM_COLOR_LIGHT_BLUE = 0x9,
    VMEM_COLOR_LIGHT_GREEN = 0xA,
    VMEM_COLOR_LIGHT_CYAN = 0xB,
    VMEM_COLOR_LIGHT_RED = 0xC,
    VEM_COLOR_LIGHT_MAGENTA = 0xD,
    VMEM_COLOR_YELLOW = 0xE,
    VMEM_COLOR_WHITE = 0xF
};

struct VMemDesign {
    uint64_t x;
    uint64_t y;
    
	enum VMemColors fg;
    enum VMemColors bg;

    aos_bool serial_out;
	aos_bool auto_scroll;
};

struct ATA_DP {
    uint64_t lba;
    uint16_t count;
};

typedef struct {
    uint64_t block_count;
    uint32_t block_size;
    char model[41];
    char serial[21];
    int supports_lba48;
	uint16_t pad;
} ata_identity_t;

void serial_init_klog(const char* path, struct pbfs_mount* mnt);
void serial_flush_klog(const char* path, struct pbfs_mount* mnt);
void serial_deinit_klog(const char* path, struct pbfs_mount* mnt);
void serial_init(aos_bool preinit);
void serial_printc(char c);
void serial_print(const char* str);
int serial_is_transmit_empty(void);
void serial_printf(const char* fmt, ...);

void vmem_init(uniboot_boot_info* sysinfo);
aos_bool is_vmem_initialized(void);
void vmem_set_only_cursor(uint64_t x, uint64_t y);
void vmem_set_cursor(struct VMemDesign* design, uint64_t x, uint64_t y);
void vmem_set_cursor_to_col(struct VMemDesign* design, uint64_t cx, uint64_t cy);
void vmem_set_cursor_ex(struct VMemDesign* design, uint64_t cx, uint64_t cy); // If Bit 63 of CX/CY is 1, CX/CY is Column based, else CX/CY is explict position
void vmem_disable_cursor(void);
void vmem_clear_screen(struct VMemDesign* design);
void vmem_printc(struct VMemDesign* design, char c);
void vmem_print(struct VMemDesign* design, const char* str);
void vmem_printf(struct VMemDesign* design, const char* fmt, ...);
void vmem_scroll_up(struct VMemDesign* design, uint32_t lines, enum VMemColors color);

aos_bool ata_exists(void);
int ata_identify_device(uint8_t drive, ata_identity_t* out_info);
int ata_read_sectors(struct ATA_DP* dp, void* buffer, uint8_t drive);
int ata_write_sectors(struct ATA_DP* dp, const void* buffer, uint8_t drive);

int is_ps2_present(void);
void ps2_init(void);
int16_t ps2_read_scan(void);
int16_t ps2_try_read_scan(void);
void ps2_read_line(char* buf, int max_len, struct VMemDesign* design);
