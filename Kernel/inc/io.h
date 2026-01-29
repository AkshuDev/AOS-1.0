#pragma once
#include <asm.h>

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
    int x;
    int y;
    enum VMemColors fg;
    enum VMemColors bg;
    uint8_t serial_out;
};

struct ATA_DP {
    uint64_t lba;
    uint16_t count;
};

void serial_init(void) __attribute__((used));
void serial_printc(char c) __attribute__((used));
void serial_print(const char* str) __attribute__((used));
int serial_is_transmit_empty(void) __attribute__((used));
void serial_printf(const char* fmt, ...) __attribute__((used));

void vmem_set_cursor(uint16_t x, uint16_t y) __attribute__((used));
void vmem_disable_cursor(void) __attribute__((used));
void vmem_clear_screen(struct VMemDesign* design) __attribute__((used));
void vmem_printc(struct VMemDesign* design, char c) __attribute__((used));
void vmem_print(struct VMemDesign* design, const char* str) __attribute__((used));
void vmem_printf(struct VMemDesign* design, const char* fmt, ...) __attribute__((used));

int ata_read_sectors(struct ATA_DP* dp, void* buffer, uint8_t drive) __attribute__((used));
int ata_write_sectors(struct ATA_DP* dp, const void* buffer, uint8_t drive) __attribute__((used));

void ps2_read_line(char* buf, int max_len, struct VMemDesign* design) __attribute__((used));
