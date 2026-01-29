#include <inttypes.h>
#include <asm.h>

#include <stdarg.h>

#include <inc/io.h>

#define SERIAL_PORT 0x3F8

// Returns n / d
static uint64_t udiv64(uint64_t n, uint64_t d) {
    uint64_t q = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        q <<= 1;
        if ((n >> i) >= d) {
            n -= d << i;
            q |= 1;
        }
    }
    return q;
}

// Returns n % d
static uint64_t umod64(uint64_t n, uint64_t d) {
    int i;
    for (i = 63; i >= 0; i--) {
        if ((n >> i) >= d) {
            n -= d << i;
        }
    }
    return n;
}

void serial_init(void) {
    asm_outb(SERIAL_PORT + 1, 0x00); // Disable interrupts
    asm_outb(SERIAL_PORT + 3, 0x80); // Enable DLAB
    asm_outb(SERIAL_PORT + 0, 0x03); // Set baud rate divisor to 3 (38400)
    asm_outb(SERIAL_PORT + 1, 0x00); 
    asm_outb(SERIAL_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    asm_outb(SERIAL_PORT + 2, 0xC7); // FIFO: enable, clear, 14-byte threshold
    asm_outb(SERIAL_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

// Check if transmit buffer is empty
int serial_is_transmit_empty(void) {
    return asm_inb(SERIAL_PORT + 5) & 0x20;
}

// Print a single character
void serial_printc(char c) {
    while (!serial_is_transmit_empty());
    asm_outb(SERIAL_PORT, c);
}

// Print a null-terminated string
void serial_print(const char* str) {
    while (*str) {
        if (*str == '\n') serial_printc('\r'); // Carriage return for terminals
        serial_printc(*str++);
    }
}

static void serial_print_integer(uint64_t val, int is_signed, int base, int uppercase) {
    char buffer[32];
    int i = 30;
    buffer[31] = 0;
    int neg = 0;

    if (is_signed && ((int64_t)val) < 0) {
        neg = 1;
        val = -(int64_t)val;
    }

    // Convert digits
    do {
        uint64_t digit = umod64(val, base);
        buffer[i--] = (digit < 10) ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
        val = udiv64(val, base);
    } while (val);

    if (neg) buffer[i--] = '-';
    serial_print(&buffer[i+1]);
}

static void serial_print_ex_integer(uint64_t val, int base, int width, int zero_pad, int is_signed) {
    char buf[64];
    const char* digits = "0123456789abcdef";
    int i = 0;
    int neg = 0;
    if (is_signed && (int64_t)val < 0) {
        neg = 1;
        val = -(int64_t)val;
    }
    do {
        buf[i++] = digits[val % base];
        val /= base;
    } while (val > 0);

    int total_len = i + (neg ? 1 : 0);
    if (width > total_len) {
        int padding_count = width - total_len;
        if (zero_pad) {
            if (neg) {
                serial_printc('-');
                neg = 0;
            }
            while (padding_count--) serial_printc('0');
        } else {
            while(padding_count--) serial_printc(' ');
        }
    }

    if (neg) serial_printc('-');
    while (i > 0) {
        serial_printc(buf[i--]);
    }
}

// Serial print with formatting
void serial_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            int zero_pad = 0;
            int width = 0;
            int is_long = 0;

            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            while (*fmt == 'l') {
                is_long++;
                fmt++;
            }

            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    serial_printc(c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    serial_print(s ? s : "(NULL)");
                    break;
                }
                case 'i':
                case 'd': { // signed 32/64-bit
                    int64_t d;
                    if (is_long >= 1) d = va_arg(args, int64_t);
                    else d = (int64_t)va_arg(args, int);
                    serial_print_ex_integer((uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    serial_print_ex_integer(u, 10, width, zero_pad, 0);
                    break;
                }
                case 'x':
                case 'p': { // Pointer
                    uint64_t p;
                    if (*fmt == 'p') {
                        p = (uintptr_t)va_arg(args, void*);
                        serial_print("0x");
                        if (width == 0) width = 16;
                        zero_pad = 1;
                    } else {
                        if (is_long >= 1) p = va_arg(args, uint64_t);
                        else p = (uint64_t)va_arg(args, uint32_t);
                    }
                    serial_print_ex_integer(p, 16, width, zero_pad, 0);
                    break;
                }
                case '%': {
                    serial_printc('%');
                    break;
                }
                default: {
                    serial_printc(*fmt);
                    break;
                }
            }
        } else {
            serial_printc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}

// VMem
#define VMEM 0xB8000
#define VMEM_MAX_COLS 80
#define VMEM_MAX_ROWS 25

void vmem_set_cursor(uint16_t x, uint16_t y) {
    uint16_t pos = y * VMEM_MAX_COLS + x;
    asm_outb(0x3D4, 0x0E);
    asm_outb(0x3D5, (pos >> 8) & 0xFF);
    asm_outb(0x3D4, 0x0F);
    asm_outb(0x3D5, pos & 0xFF);
}

void vmem_disable_cursor(void) {
    asm_outb(0x3D4, 0x0A);
    asm_outb(0x3D5, 0x20);
}

void vmem_clear_screen(struct VMemDesign* design) {
    volatile uint16_t* vmem = (volatile uint16_t*)VMEM;
    uint16_t attr = (design->bg << 4) | design->fg;
    for (uint16_t i = 0; i < VMEM_MAX_COLS*VMEM_MAX_ROWS; i++) vmem[i] = attr << 8;
    design->x = 0;
    design->y = 0;
}

void vmem_printc(struct VMemDesign* design, char c) {
    volatile uint16_t* vmem = (volatile uint16_t*)VMEM;
    uint16_t attr = (design->bg << 4) | design->fg;
    if (c == '\n') {
        design->x = 0;
        uint16_t y = design->y + 1;
        vmem_set_cursor(design->x, y);
        if (design->serial_out == 1) serial_printc(c);
        return;
    }
    vmem[design->y * VMEM_MAX_COLS + design->x] = ((uint16_t)attr << 8) | c;
    uint16_t x = design->x + 1;
    vmem_set_cursor(x, design->y);
    if (design->serial_out == 1) serial_printc(c);
}

void vmem_print(struct VMemDesign* design, const char* str) {
    while (*str) vmem_printc(design, *str++);
}

static void vmem_print_integer(struct VMemDesign* design, uint64_t val, int is_signed, int base, int uppercase) {
    char buffer[32];
    int i = 30;
    buffer[31] = 0;
    int neg = 0;

    if (is_signed && ((int64_t)val) < 0) {
        neg = 1;
        val = -(int64_t)val;
    }

    // Convert digits
    do {
        uint64_t digit = umod64(val, base);
        buffer[i--] = (digit < 10) ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
        val = udiv64(val, base);
    } while (val);

    if (neg) buffer[i--] = '-';
    vmem_print(design, &buffer[i+1]);
}

static void vmem_print_ex_integer(struct VMemDesign* design, uint64_t val, int base, int width, int zero_pad, int is_signed) {
    char buf[64];
    const char* digits = "0123456789abcdef";
    int i = 0;
    int neg = 0;
    if (is_signed && (int64_t)val < 0) {
        neg = 1;
        val = -(int64_t)val;
    }
    do {
        buf[i++] = digits[val % base];
        val /= base;
    } while (val > 0);

    int total_len = i + (neg ? 1 : 0);
    if (width > total_len) {
        int padding_count = width - total_len;
        if (zero_pad) {
            if (neg) {
                vmem_printc(design, '-');
                neg = 0;
            }
            while (padding_count--) vmem_printc(design, '0');
        } else {
            while(padding_count--) vmem_printc(design, ' ');
        }
    }

    if (neg) vmem_printc(design, '-');
    while (i > 0) {
        vmem_printc(design, buf[i--]);
    }
}

void vmem_printf(struct VMemDesign* design, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            int zero_pad = 0;
            int width = 0;
            int is_long = 0;

            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            while (*fmt == 'l') {
                is_long++;
                fmt++;
            }

            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    vmem_printc(design, c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    vmem_print(design, s ? s : "(NULL)");
                    break;
                }
                case 'i':
                case 'd': { // signed 32/64-bit
                    int64_t d;
                    if (is_long >= 1) d = va_arg(args, int64_t);
                    else d = (int64_t)va_arg(args, int);
                    vmem_print_ex_integer(design, (uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    vmem_print_ex_integer(design, u, 10, width, zero_pad, 0);
                    break;
                }
                case 'x':
                case 'p': { // Pointer
                    uint64_t p;
                    if (*fmt == 'p') {
                        p = (uintptr_t)va_arg(args, void*);
                        serial_print("0x");
                        if (width == 0) width = 16;
                        zero_pad = 1;
                    } else {
                        if (is_long >= 1) p = va_arg(args, uint64_t);
                        else p = (uint64_t)va_arg(args, uint32_t);
                    }
                    vmem_print_ex_integer(design, p, 16, width, zero_pad, 0);
                    break;
                }
                case '%': {
                    vmem_printc(design, '%');
                    break;
                }
                default: {
                    vmem_printc(design, *fmt);
                    break;
                }
            }
        } else {
            vmem_printc(design, *fmt);
        }
        fmt++;
    }

    va_end(args);
}

// ATA stuff
// I/O port addresses for the primary ATA bus.
#define ATA_PRIMARY_BASE 0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6

// I/O ports relative to the base address
#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECTOR_COUNT 0x02
#define ATA_REG_LBA_LOW 0x03
#define ATA_REG_LBA_MID 0x04
#define ATA_REG_LBA_HIGH 0x05
#define ATA_REG_DRIVE_HEAD 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

// Status Register Bits
#define ATA_SR_BSY 0x80 // Busy
#define ATA_SR_DRDY 0x40 // Drive Ready
#define ATA_SR_DF 0x20 // Device Fault
#define ATA_SR_DRQ 0x08 // Data Request Ready
#define ATA_SR_ERR 0x01 // Error

// ATA Commands
#define ATA_CMD_READ_PIO_EXT 0x24 // LBA48 Read
#define ATA_CMD_WRITE_PIO_EXT 0x34 // LBA48 Write
#define ATA_CMD_FLUSH_EXT 0xEA // LBA48 Flush

// ATA Ports
#define ATA_PRIMARY_DATA 0x1F0
#define ATA_PRIMARY_ERROR 0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW 0x1F3
#define ATA_PRIMARY_LBA_MID 0x1F4
#define ATA_PRIMARY_LBA_HIGH 0x1F5
#define ATA_PRIMARY_DRIVE_HEAD 0x1F6
#define ATA_PRIMARY_COMMAND 0x1F7
#define ATA_PRIMARY_STATUS 0x1F7 // Status register is at the same address as Command
#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
} ata_bus_t;

static const ata_bus_t ata_buses[2] = {
    { ATA_PRIMARY_IO, ATA_PRIMARY_CTRL },
    { ATA_SECONDARY_IO, ATA_SECONDARY_CTRL }
};

// Wait for ATA (PRIVATE DO NOT USE UNLESS YOU KNOW WHAT YOU ARE DOING)
static inline void _ata_wait_ready(uint16_t io_base) {
    uint8_t status;
    int timeout = 100000; // ~100ms depending on CPU speed

    while (timeout--) {
        status = asm_inb(io_base + 7);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY))
            return; // Ready!
    }
}

int ata_read_sectors(struct ATA_DP* dp, void* buffer, uint8_t drive) {
    uint8_t bus = ((drive - 0x80) >> 1) & 1;
    uint8_t device = (drive & 1); // 0=master,1=slave
    uint16_t io = ata_buses[bus].io_base;
    uint16_t ctrl = ata_buses[bus].ctrl_base;

    _ata_wait_ready(io);

    // Send the LBA48 and sector count high byte first
    asm_outb(ctrl, 0x00); // Clear high order bytes
    asm_outb(io + 2, (dp->count >> 8) & 0xFF);
    asm_outb(io + 3, (dp->lba >> 24) & 0xFF);
    asm_outb(io + 4, (dp->lba >> 32) & 0xFF);
    asm_outb(io + 5, (dp->lba >> 40) & 0xFF);

    // Send the LBA48 low bytes and sector count low byte.
    asm_outb(io + 6, 0x40 | (device << 4)); // Choose drive

    asm_outb(io + 2, dp->count & 0xFF);
    asm_outb(io + 3, dp->lba & 0xFF);
    asm_outb(io + 4, (dp->lba >> 8) & 0xFF);
    asm_outb(io + 5, (dp->lba >> 16) & 0xFF);

    // Issue the LBA48 Read command.
    asm_outb(io + 7, ATA_CMD_READ_PIO_EXT);

    // Read each sector.
    for (int i = 0; i < dp->count; i++) {
        _ata_wait_ready(io);
        uint8_t status = asm_inb(io + 7);

        // Check for errors.
        if (status & ATA_SR_ERR) { // Error
            return -1;
        }

        // Wait for Data Request Ready.
        while (!(asm_inb(io + 7) & ATA_SR_DRQ));

        // Read 256 words (512 bytes) into the buffer.
        asm_insw(io, (uint8_t*)buffer + (uint32_t)i * 512, 256);
    }

    return 0; // Success
}

int ata_write_sectors(struct ATA_DP* dp, const void* buffer, uint8_t drive) {
    uint8_t bus = ((drive - 0x80) >> 1) & 1;
    uint8_t device = (drive & 1);
    uint16_t io = ata_buses[bus].io_base;
    uint16_t ctrl = ata_buses[bus].ctrl_base;

    _ata_wait_ready(io);

    // Same as read
    asm_outb(ctrl, 0x00);
    asm_outb(io + 2, (dp->count >> 8) & 0xFF);
    asm_outb(io + 3, (dp->lba >> 24) & 0xFF);
    asm_outb(io + 4, (dp->lba >> 32) & 0xFF);
    asm_outb(io + 5, (dp->lba >> 40) & 0xFF);

    asm_outb(io + 6, 0x40 | (device << 4));
    asm_outb(io + 2, dp->count & 0xFF);
    asm_outb(io + 3, dp->lba & 0xFF);
    asm_outb(io + 4, (dp->lba >> 8) & 0xFF);
    asm_outb(io + 5, (dp->lba >> 16) & 0xFF);

    // Issue the LBA48 Write command.
    asm_outb(io + 7, ATA_CMD_WRITE_PIO_EXT);

    // Write each sector.
    for (int i = 0; i < dp->count; i++) {
        _ata_wait_ready(io);
        uint8_t status = asm_inb(io + 7);

        if (status & ATA_SR_ERR) {
            return -1;
        }

        while (!(asm_inb(io + 7) & ATA_SR_DRQ));

        // Write 256 words (512 bytes) from the buffer.
        asm_outsw(io, (uint8_t*)buffer + (uint32_t)i * 512, 256);
    }

    // Flush the cache to ensure data is written to disk.
    asm_outb(io + 7, ATA_CMD_FLUSH_EXT);
    _ata_wait_ready(io);

    return 0; // Success
}

// PS2
// Scan Codes
static const char scan_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static int8_t ps2_read_scan(void) {
    while (!(asm_inb(0x64) & 1)); // wait for data
    return asm_inb(0x60);
}

void ps2_read_line(char* buf, int max_len, struct VMemDesign* design) {
    int idx = 0;
    for (;;) {
        uint8_t sc = ps2_read_scan(); // read a scan code
        char c = 0;

        // Ignore key releases (high bit set)
        if (sc & 0x80) continue;

        c = scan_to_ascii[sc];
        if (!c) continue;

        if (c == '\n') { // Enter
            buf[idx] = 0;
            vmem_printc(design, '\n');
            break;
        } else if (c == '\b') { // Backspace
            if (idx > 0) {
                idx--;
                // Move cursor back visually
                if (design->x == 0 && design->y > 0) {
                    design->y--;
                    design->x = VMEM_MAX_COLS - 1;
                } else if (design->x > 0) {
                    design->x--;
                }
                vmem_set_cursor(design->x, design->y);
                vmem_printc(design, ' '); // erase
                design->x--;
                vmem_set_cursor(design->x, design->y);
            }
        } else {
            if (idx < max_len - 1) {
                buf[idx++] = c;
                vmem_printc(design, c); // echo typed char
            }
        }
    }
}

