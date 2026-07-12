#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>

#include <stdarg.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/keyboard/keyboard.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#ifdef PBFS_WDRIVERS
	#undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>

#define SERIAL_PORT 0x3F8
#define SERIAL_SCRATCH_PORT (SERIAL_PORT + 7)

static spinlock_t serial_lock;
static spinlock_t serialf_lock;
static spinlock_t serialc_lock;
static spinlock_t ata_lock;
static spinlock_t vmem_lock;
static spinlock_t vmemf_lock;
static spinlock_t vmemc_lock;
static spinlock_t vmem_cur_lock;
static spinlock_t ps2_lock;

static aos_bool serial_present;

uint64_t IO_VMEM_MAX_COLS_true;
uint64_t IO_VMEM_MAX_ROWS_true;
uint64_t IO_VMEM_true;
static uint8_t vmem_mode;
static FB_Info_t vmem_fbi;
static FB_Cursor_t vmem_fbc;

// Kernel Log

// Log Format->
// Format Per msg = [%OS - %Timestamp] %msg

static char preklog[0x1000]; // 4KB - Pre load KLOG

static char* klog; // 4KB Log
static uint64_t klog_cap;
static uint64_t klog_end;
static uint64_t klog_pos;
static aos_bool klog_present;
static aos_bool klog_msg_started;

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

static aos_bool klog_realloc(void) {
	if ((uint64_t)klog == (uint64_t)preklog) return AOS_FALSE;

	if (klog_cap - klog_pos >= 32) return AOS_TRUE;
	size_t size = klog_cap;
	char* nptr = (char*)avmf_alloc(size + 1024, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL); // +1KB
	if (!nptr) return AOS_FALSE;

	memcpy(nptr, klog, klog_end);
	avmf_free((uint64_t)klog);
	
	klog = nptr;
	klog_cap = size + 1024;
	return AOS_TRUE;
}

static void klog_printc(char c) {
	if (!klog) return;

	if (klog_pos + 32 >= klog_cap) { if (!klog_realloc()) return; }
	if (!klog_msg_started) {
		memcpy(&klog[klog_pos], "\n[AOS - ", 8); klog_pos += 8; // \n prevents collisions with prev logs as well as prev data

		uint64_t val = kget_timestamp_seconds();
		char buf[64];
		const char* digits = "0123456789";
		int i = 0;
		do {
			buf[i++] = digits[val % 10];
			val /= 10;
		} while (val > 0);

		int total_len = i;
		if (10 > total_len) {
			int padding_count = 10 - total_len;
			while (padding_count--) klog[klog_pos++] = '0';
		}

		while (i > 0) {
			klog[klog_pos++] = buf[--i];
		}

		memcpy(&klog[klog_pos], "] ", 2); klog_pos += 2;
		klog_msg_started = AOS_TRUE;
	}

	klog[klog_pos++] = c;
	if (klog_end < klog_pos) klog_end = klog_pos;
}

void serial_init(aos_bool pre_init) {
	if (pre_init) {
		serial_lock = 0;
		serialf_lock = 0;
		serialc_lock = 0;
		ata_lock = 0;
		vmem_lock = 0;
		vmemf_lock = 0;
		vmemc_lock = 0;
		vmem_cur_lock = 0;
		ps2_lock = 0;
	}

    asm_outb(SERIAL_PORT + 1, 0x00); // Disable interrupts
    asm_outb(SERIAL_PORT + 3, 0x80); // Enable DLAB
    asm_outb(SERIAL_PORT + 0, 0x03); // Set baud rate divisor to 3 (38400)
    asm_outb(SERIAL_PORT + 1, 0x00); 
    asm_outb(SERIAL_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    asm_outb(SERIAL_PORT + 2, 0xC7); // FIFO: enable, clear, 14-byte threshold
    asm_outb(SERIAL_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set

	asm_outb(SERIAL_SCRATCH_PORT, 0xAE);

    if (asm_inb(SERIAL_SCRATCH_PORT) == 0xAE)
        serial_present = AOS_TRUE;
    else
        serial_present = AOS_FALSE;

	klog = preklog;
	klog_msg_started = AOS_FALSE;
	klog_cap = sizeof(preklog);
	if (pre_init) {
		klog_pos = 0;
		klog_end = 0;
	}
}

void serial_init_klog(const char* path, struct pbfs_mount* mnt) {
	if (!mnt) return;

	PBFS_DMM_Entry out;
	uint64_t out_lba = 0;

	uint8_t* data = NULL;
	aos_bool preklog_present = klog_end > 0;

	size_t size = 0;
	klog_present = AOS_FALSE;
	if (pbfs_find_entry(path, &out, &out_lba, mnt) == PBFS_RES_SUCCESS && out.type & METADATA_FLAG_SYS) {
		pbfs_read_file(mnt, path, &data, &size);
		klog_present = AOS_TRUE;
	}

	klog = (char*)avmf_alloc(size + klog_end + 4096, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL); // +4KB
	if (!klog) return;
	if (preklog_present) memcpy(klog, preklog, klog_end);
	if (data && size > 0) memcpy(klog + klog_end, data, size);
	klog_pos = size + klog_end;
	klog_cap = size + 4096 + klog_end;
	klog_end = size + klog_end;
}

void serial_flush_klog(const char* path, struct pbfs_mount* mnt) {
	if (!klog) return;
	if (klog_end <= 0) {
		return;
	}
	
	if (klog_present) pbfs_update_file(mnt, path, klog, klog_end);
	else pbfs_add(mnt, path, 0, 0, METADATA_FLAG_SYS, PERM_READ | PERM_WRITE, klog, klog_end);
}

void serial_deinit_klog(const char* path, struct pbfs_mount* mnt) {
	if (!klog) return;
	if (klog_end <= 0) {
		if ((uint64_t)klog != (uint64_t)preklog) avmf_free((uint64_t)klog);
		klog = NULL;
		klog_end = 0;
		klog_pos = 0;
		klog_cap = 0;
		return;
	}
	
	if (klog_present) pbfs_update_file(mnt, path, klog, klog_end);
	else pbfs_add(mnt, path, 0, 0, METADATA_FLAG_SYS, PERM_READ | PERM_WRITE, klog, klog_end);
	if ((uint64_t)klog != (uint64_t)preklog) avmf_free((uint64_t)klog);
	klog = NULL;
	klog_end = 0;
	klog_pos = 0;
	klog_cap = 0;
	klog_present = AOS_TRUE;
}

// Check if transmit buffer is empty
int serial_is_transmit_empty(void) {
    return asm_inb(SERIAL_PORT + 5) & 0x20;
}

// Print a single character
void serial_printc(char c) {
    uint64_t rflags = spin_lock_irqsave(&serialc_lock);
    
	if (serial_present) {
		while (!serial_is_transmit_empty());
    	asm_outb(SERIAL_PORT, c);
	}

	klog_printc(c);

    spin_unlock_irqrestore(&serialc_lock, rflags);
}

// Print a null-terminated string
void serial_print(const char* str) {
    uint64_t rflags = spin_lock_irqsave(&serial_lock);
    while (*str) {
        if (*str == '\n') serial_printc('\r'); // Carriage return for terminals
        serial_printc(*str++);
    }
    klog_msg_started = AOS_FALSE;
	spin_unlock_irqrestore(&serial_lock, rflags);
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
        serial_printc(buf[--i]);
    }
}

// Serial print with formatting
void serial_printf(const char* fmt, ...) {
    uint64_t rflags = spin_lock_irqsave(&serialf_lock);
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
					klog_msg_started = AOS_TRUE;
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
						klog_msg_started = AOS_TRUE;
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

	klog_msg_started = AOS_FALSE;
    spin_unlock_irqrestore(&serialf_lock, rflags);
}

// VMem
static uint32_t pyrion_convert_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a, enum pyrion_color_format fmt){
    switch (fmt) {
        case PYRION_COLORF_RGBA:
            return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;

        case PYRION_COLORF_BGRA:
            return ((uint32_t)b << 24) | ((uint32_t)g << 16) | ((uint32_t)r << 8) | a;

        case PYRION_COLORF_ABGR:
            return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;

        case PYRION_COLORF_ARGB:
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;

        case PYRION_COLORF_RGB:
            return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;

        case PYRION_COLORF_BGR:
            return ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;

        default:
            return 0;
    }
}

static uint32_t vmem_convert_color_to_rgba(enum VMemColors color){
	enum pyrion_color_format fmt = vmem_fbi.cformat;
    switch (color) {
        case VMEM_COLOR_BLACK:
            return pyrion_convert_color(0x00,0x00,0x00,0xFF, fmt);

        case VMEM_COLOR_BLUE:
            return pyrion_convert_color(0x00,0x00,0xAA,0xFF, fmt);

        case VMEM_COLOR_GREEN:
            return pyrion_convert_color(0x00,0xAA,0x00,0xFF, fmt);

        case VMEM_COLOR_CYAN:
            return pyrion_convert_color(0x00,0xAA,0xAA,0xFF, fmt);

        case VMEM_COLOR_RED:
            return pyrion_convert_color(0xAA,0x00,0x00,0xFF, fmt);

        case VMEM_COLOR_MAGENTA:
            return pyrion_convert_color(0xAA,0x00,0xAA,0xFF, fmt);

        case VMEM_COLOR_BROWN:
            return pyrion_convert_color(0xAA,0x55,0x00,0xFF, fmt);

        case VMEM_COLOR_LIGHT_GRAY:
            return pyrion_convert_color(0xAA,0xAA,0xAA,0xFF, fmt);

        case VMEM_COLOR_DARK_GRAY:
            return pyrion_convert_color(0x55,0x55,0x55,0xFF, fmt);

        case VMEM_COLOR_LIGHT_BLUE:
            return pyrion_convert_color(0x55,0x55,0xFF,0xFF, fmt);

        case VMEM_COLOR_LIGHT_GREEN:
            return pyrion_convert_color(0x55,0xFF,0x55,0xFF, fmt);

        case VMEM_COLOR_LIGHT_CYAN:
            return pyrion_convert_color(0x55,0xFF,0xFF,0xFF, fmt);

        case VMEM_COLOR_LIGHT_RED:
            return pyrion_convert_color(0xFF,0x55,0x55,0xFF, fmt);

        case VEM_COLOR_LIGHT_MAGENTA:
            return pyrion_convert_color(0xFF,0x55,0xFF,0xFF, fmt);

        case VMEM_COLOR_YELLOW:
            return pyrion_convert_color(0xFF,0xFF,0x55,0xFF, fmt);

        case VMEM_COLOR_WHITE:
            return pyrion_convert_color(0xFF,0xFF,0xFF,0xFF, fmt);

        default:
            return pyrion_convert_color(0x00,0x00,0x00,0xFF, fmt);
    }
}

void vmem_init(aos_sysinfo_t* sysinfo) {
	vmem_fbc = (FB_Cursor_t){0};
	if (!sysinfo) {
		vmem_mode = AOS_SYSINFO_FB_MODE_VGA;
		IO_VMEM_true = IO_VMEM;
		IO_VMEM_MAX_COLS_true = IO_VMEM_MAX_COLS;
		IO_VMEM_MAX_ROWS_true = IO_VMEM_MAX_ROWS;
		vmem_fbi = (FB_Info_t){0};

		return;
	}

	vmem_mode = sysinfo->fb_mode;
	switch (vmem_mode) {
		case AOS_SYSINFO_FB_MODE_VGA:
		case AOS_SYSINFO_FB_MODE_FB: break;
		default: vmem_mode = AOS_SYSINFO_FB_MODE_VGA; break;
	}
	IO_VMEM_true = AOS_DIRECT_MAP_BASE + sysinfo->fb_info.phys_addr;
	IO_VMEM_MAX_COLS_true = sysinfo->fb_info.width;
	IO_VMEM_MAX_ROWS_true = sysinfo->fb_info.height;

	vmem_fbi = sysinfo->fb_info;
	vmem_fbi.addr = IO_VMEM_true;

	pager_map_range(IO_VMEM_true, sysinfo->fb_info.phys_addr, sysinfo->fb_info.size > 0 ? sysinfo->fb_info.size : IO_VMEM_MAX_COLS_true * IO_VMEM_MAX_ROWS_true * sizeof(uint32_t), PAGE_PRESENT | PAGE_PCD | PAGE_RW);

	serial_printf("[IO:VMEM] FB Mode: %u\n", sysinfo->fb_mode);
	serial_printf("[IO:VMEM] FB Addr: 0x%llx\n", sysinfo->fb_info.addr);
	serial_printf("[IO:VMEM] FB Width: %u\n", sysinfo->fb_info.width);
	serial_printf("[IO:VMEM] FB Height: %u\n", sysinfo->fb_info.height);
	serial_printf("[IO:VMEM] FB Pitch: %u\n", sysinfo->fb_info.pitch);
	serial_printf("[IO:VMEM] FB Size: %u\n", sysinfo->fb_info.size);
}

void vmem_set_cursor(uint16_t x, uint16_t y) {
    uint64_t rflags = spin_lock_irqsave(&vmem_cur_lock);

	if (vmem_mode == AOS_SYSINFO_FB_MODE_FB) {
		vmem_fbc.x = x;
		vmem_fbc.y = y;

		spin_unlock_irqrestore(&vmem_cur_lock, rflags);
		return;
	}

    uint16_t pos = y * IO_VMEM_MAX_COLS_true + x;
    asm_outb(0x3D4, 0x0E);
    asm_outb(0x3D5, (pos >> 8) & 0xFF);
    asm_outb(0x3D4, 0x0F);
    asm_outb(0x3D5, pos & 0xFF);

    spin_unlock_irqrestore(&vmem_cur_lock, rflags);
}

void vmem_disable_cursor(void) {
	if (vmem_mode != AOS_SYSINFO_FB_MODE_VGA) return;

    uint64_t rflags = spin_lock_irqsave(&vmem_cur_lock);

    asm_outb(0x3D4, 0x0A);
    asm_outb(0x3D5, 0x20);

    spin_unlock_irqrestore(&vmem_cur_lock, rflags);
}

void vmem_clear_screen(struct VMemDesign* design) {
    uint64_t rflags = spin_lock_irqsave(&vmem_lock);

	if (vmem_mode == AOS_SYSINFO_FB_MODE_FB) {
		vmem_fbc.x = design->x;
		vmem_fbc.y = design->y;
		vmem_fbc.bg_color = vmem_convert_color_to_rgba(design->bg);
		vmem_fbc.fg_color = vmem_convert_color_to_rgba(design->fg);
		
		fb_clear(&vmem_fbi, vmem_fbc.bg_color);

		vmem_fbc.x = 0;
		vmem_fbc.y = 0;
		design->x = 0;
    	design->y = 0;

		spin_unlock_irqrestore(&vmem_lock, rflags);
		return;
	}

    volatile uint16_t* vmem = (volatile uint16_t*)IO_VMEM_true;
    uint16_t attr = (design->bg << 4) | design->fg;
    for (uint16_t i = 0; i < IO_VMEM_MAX_COLS_true*IO_VMEM_MAX_ROWS_true; i++) vmem[i] = attr << 8;
    design->x = 0;
    design->y = 0;

    spin_unlock_irqrestore(&vmem_lock, rflags);
}

void vmem_printc(struct VMemDesign* design, char c) {
	if (design->serial_out) {
		serial_printc(c);
		if (c == '\b') {
			serial_printc(' ');
			serial_printc('\b');
		}
	}

	if (design->x > IO_VMEM_MAX_COLS_true) design->x = IO_VMEM_MAX_COLS_true;
	else if (design->x < 0) design->x = 0;
	if (design->y > IO_VMEM_MAX_ROWS_true) design->y = IO_VMEM_MAX_ROWS_true;
	else if (design->y < 0) design->y = 0;

	vmem_set_cursor(design->x, design->y);

    uint64_t rflags = spin_lock_irqsave(&vmemc_lock);

	if (vmem_mode == AOS_SYSINFO_FB_MODE_FB) {
		vmem_fbc.bg_color = vmem_convert_color_to_rgba(design->bg);
		vmem_fbc.fg_color = vmem_convert_color_to_rgba(design->fg);
		
		fb_printc(&vmem_fbi, &vmem_fbc, c);
		design->x = vmem_fbc.x;
		design->y = vmem_fbc.y;

		spin_unlock_irqrestore(&vmemc_lock, rflags);
		return;
	}

    volatile uint16_t* vmem = (volatile uint16_t*)IO_VMEM_true;
    uint16_t attr = (design->bg << 4) | design->fg;
    if (c == '\n') {
        design->x = 0;
        design->y++;
        spin_unlock_irqrestore(&vmemc_lock, rflags);
        return;
    } else if (c == '\b') {
		if (design->x == 0 && design->y == 0) {
			spin_unlock_irqrestore(&vmemc_lock, rflags);
			return;
		}
		if (design->x > 0) design->x--;
		else {
			design->x = IO_VMEM_MAX_COLS_true;
			design->y--;
		}

        vmem_set_cursor(design->x, design->y);
		vmem[design->y * IO_VMEM_MAX_COLS_true + design->x] = ((uint16_t)attr << 8) | ' ';

		spin_unlock_irqrestore(&vmemc_lock, rflags);
        return;
	}
    vmem[design->y * IO_VMEM_MAX_COLS_true + design->x] = ((uint16_t)attr << 8) | c;
    design->x++;

    spin_unlock_irqrestore(&vmemc_lock, rflags);
}

void vmem_print(struct VMemDesign* design, const char* str) {
    uint64_t rflags = spin_lock_irqsave(&vmem_lock);

    while (*str) vmem_printc(design, *str++);
    spin_unlock_irqrestore(&vmem_lock, rflags);
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
        vmem_printc(design, buf[--i]);
    }
}

void vmem_printf(struct VMemDesign* design, const char* fmt, ...) {
    uint64_t rflags = spin_lock_irqsave(&vmemf_lock);

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
                        vmem_print(design, "0x");
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
    spin_unlock_irqrestore(&vmemf_lock, rflags);
}
void vmem_scroll_up(struct VMemDesign* design, uint32_t top, uint32_t bottom, uint32_t width) {
	if (vmem_mode == AOS_SYSINFO_FB_MODE_FB) {
		design->x = 0;
		design->y = 0;
		return;
	}
    uint16_t* vmem = (uint16_t*)IO_VMEM_true;
    uint32_t stride = IO_VMEM_MAX_COLS_true; 

    for (size_t y = top; y < bottom - 1; y++) {
        for (size_t x = 0; x < width; x++) {
            vmem[y * stride + x] = vmem[(y + 1) * stride + x];
        }
    }

    uint16_t blank_attr = (uint8_t)design->fg | (uint8_t)(design->bg << 4);
    uint16_t blank_char = (uint16_t)' ' | (uint16_t)(blank_attr << 8);

    for (size_t x = 0; x < width; x++) {
        vmem[(bottom - 1) * stride + x] = blank_char;
    }
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
#define ATA_CMD_IDENTIFY 0xEC // Identiify
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

static inline void _ata_wait_ready(uint16_t io_base) {
    uint8_t status;
    uint64_t timeout = kget_ms_passed();

    while (kget_ms_passed() - timeout < 1000) {
        status = asm_inb(io_base + 7);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY))
            return; // Ready!
    }
}

aos_bool ata_exists(void) {
	uint64_t rflags = spin_lock_irqsave(&ata_lock);
	aos_bool exists = asm_inb(ATA_PRIMARY_STATUS) == 0xFF ? 0 : 1;
	spin_unlock_irqrestore(&ata_lock, rflags);
	return exists;
}

int ata_identify_device(uint8_t drive, ata_identity_t* out_info) {
    uint16_t buffer[256];
    uint64_t rflags = spin_lock_irqsave(&ata_lock);

    uint8_t bus = ((drive - 0x80) >> 1) & 1;
    uint8_t device = (drive & 1); // 0=master, 1=slave
    uint16_t io = ata_buses[bus].io_base;

    asm_outb(io + 6, 0xA0 | (device << 4));
    
    for(int i = 0; i < 4; i++) asm_inb(io + 7);

    asm_outb(io + 2, 0);
    asm_outb(io + 3, 0);
    asm_outb(io + 4, 0);
    asm_outb(io + 5, 0);

    asm_outb(io + 7, ATA_CMD_IDENTIFY);
    
    uint8_t status = asm_inb(io + 7);
    if (status == 0) {
        spin_unlock_irqrestore(&ata_lock, rflags);
        return 0;
    }

    _ata_wait_ready(io);
    status = asm_inb(io + 7);

    if (status & ATA_SR_ERR) {
        spin_unlock_irqrestore(&ata_lock, rflags);
        return 0;
    }

	uint64_t timeout = kget_ms_passed();
	while (!(asm_inb(io + 7) & ATA_SR_DRQ)) {
		if (kget_ms_passed() - timeout >= 1000) {
			spin_unlock_irqrestore(&ata_lock, rflags);
			return -1;
		}
	}
    asm_insw(io, buffer, 256);

    out_info->block_size = 512;

    for (int i = 0; i < 20; i++) {
        out_info->model[i * 2] = (char)(buffer[27 + i] >> 8);
        out_info->model[i * 2 + 1] = (char)(buffer[27 + i] & 0xFF);
    }
    out_info->model[40] = '\0';

    for (int i = 0; i < 10; i++) {
        out_info->serial[i * 2] = (char)(buffer[10 + i] >> 8);
        out_info->serial[i * 2 + 1] = (char)(buffer[10 + i] & 0xFF);
    }
    out_info->serial[20] = '\0';

    for (int i = 39; i >= 0 && out_info->model[i] == ' '; i--) out_info->model[i] = '\0';
    for (int i = 19; i >= 0 && out_info->serial[i] == ' '; i--) out_info->serial[i] = '\0';

    uint32_t lba28_sectors = *((uint32_t*)&buffer[60]);
    uint64_t lba48_sectors = *((uint64_t*)&buffer[100]);
    uint16_t lba48_supported = buffer[83];

    if ((lba48_supported & (1 << 10)) && lba48_sectors > 0) {
        out_info->block_count = lba48_sectors;
        out_info->supports_lba48 = 1;
    } else {
        out_info->block_count = lba28_sectors;
        out_info->supports_lba48 = 0;
    }

    spin_unlock_irqrestore(&ata_lock, rflags);
    return 1;
}

int ata_read_sectors(struct ATA_DP* dp, void* buffer, uint8_t drive) {
    uint64_t rflags = spin_lock_irqsave(&ata_lock);

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
            spin_unlock_irqrestore(&ata_lock, rflags);
            return -1;
        }

        // Wait for Data Request Ready.
		uint64_t timeout = kget_ms_passed();
        while (!(asm_inb(io + 7) & ATA_SR_DRQ)) {
			if (kget_ms_passed() - timeout >= 1000) {
				spin_unlock_irqrestore(&ata_lock, rflags);
				return -1;
			}
		}

        // Read 256 words (512 bytes) into the buffer.
        asm_insw(io, (uint8_t*)buffer + (uint32_t)i * 512, 256);
    }
    spin_unlock_irqrestore(&ata_lock, rflags);

    return 0; // Success
}

int ata_write_sectors(struct ATA_DP* dp, const void* buffer, uint8_t drive) {
    uint64_t rflags = spin_lock_irqsave(&ata_lock);

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
            spin_unlock_irqrestore(&ata_lock, rflags);
            return -1;
        }

		uint64_t timeout = kget_ms_passed();
        while (!(asm_inb(io + 7) & ATA_SR_DRQ)) {
			if (kget_ms_passed() - timeout >= 1000) {
				spin_unlock_irqrestore(&ata_lock, rflags);
				return -1;
			}
		}

        // Write 256 words (512 bytes) from the buffer.
        asm_outsw(io, (uint8_t*)buffer + (uint32_t)i * 512, 256);
    }

    // Flush the cache to ensure data is written to disk.
    asm_outb(io + 7, ATA_CMD_FLUSH_EXT);
    _ata_wait_ready(io);

    spin_unlock_irqrestore(&ata_lock, rflags);

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

int is_ps2_present(void) {
    uint64_t rflags = spin_lock_irqsave(&ps2_lock);

    int out = 0;
    while (asm_inb(0x64) & 0x01) { (void)asm_inb(0x60); }
    asm_outb(0x60, 0xEE);
    for (int i = 0; i < 500000; i++) {
        if (asm_inb(0x64) & 0x01) {
            uint8_t resp = asm_inb(0x60);
            if (resp == 0xEE) { out = 1; break; } // keyboard present
            else { out = 0; break; };
        }
    }

    spin_unlock_irqrestore(&ps2_lock, rflags);
    return out;
}

static void ps2_wait_input_empty(void) {
    while (asm_inb(0x64) & 0x02)
        asm volatile("pause");
}

static void ps2_wait_output_full(void) {
    while (!(asm_inb(0x64) & 0x01))
        asm volatile("pause");
}

static void keyboard_send(uint8_t val) {
    ps2_wait_input_empty();
    asm_outb(0x60, val);
}

static uint8_t keyboard_read(void) {
    ps2_wait_output_full();
    return asm_inb(0x60);
}

void ps2_init(void) {
    uint64_t rflags = spin_lock_irqsave(&ps2_lock);

    uint8_t config;
    ps2_wait_input_empty();
    asm_outb(0x64, 0x20);
    while (!(asm_inb(0x64) & 0x01)) {}
    config = asm_inb(0x60);

    // Set bits: bit 0 = interrupt on keyboard, bit 4 = enable keyboard port
    config |= (1 << 0);
	config &= ~(1 << 4);

    // Write config byte back
    ps2_wait_input_empty();
    asm_outb(0x64, 0x60);
    ps2_wait_input_empty();
    asm_outb(0x60, config);

	keyboard_send(0xF0);
	if (keyboard_read() == 0xFA) {
		keyboard_send(0x01);
	}

    // Send enable scanning command to keyboard
    keyboard_send(0xF4);

    spin_unlock_irqrestore(&ps2_lock, rflags);
}

int16_t ps2_read_scan(void) {
    uint64_t rflags = spin_lock_irqsave(&ps2_lock);

    while (!(asm_inb(0x64) & 1)); // wait for data
    int16_t out = (int16_t)asm_inb(0x60);
    spin_unlock_irqrestore(&ps2_lock, rflags);

    return out;
}

int16_t ps2_try_read_scan(void) {
    uint64_t rflags = spin_lock_irqsave(&ps2_lock);
    
    if (!(asm_inb(0x64) & 1)) {
        spin_unlock_irqrestore(&ps2_lock, rflags);
        return -1;
    }
    int16_t out = (int16_t)asm_inb(0x60);
    spin_unlock_irqrestore(&ps2_lock, rflags);

    return out;
}

void ps2_read_line(char* buf, int max_len, struct VMemDesign* design) {
    int idx = 0;
    for (;;) {
        char c = keyboard_ps2_get_char();
        if (!c) continue;

        if (c == '\n') { // Enter
            buf[idx] = 0;
            vmem_printc(design, '\n');
            break;
        } else if (c == '\b') { // Backspace
            if (idx > 0) {
                idx--;
				// Move cursor back visually
                vmem_printc(design, '\b'); // erase
            }
        } else {
            if (idx < max_len - 1) {
                buf[idx++] = c;
                vmem_printc(design, c); // echo typed char
            }
        }
    }
}

