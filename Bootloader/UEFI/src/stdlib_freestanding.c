#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <system.h>

#include <pefi.h>
#include <pefilib.h>
#include <pefi_simple_text_out.h>
#include <pefi_simple_text_in.h>

#include <freestanding.h>

EFIAPI void* memcpy(void* dest, const void* src, size_t n) {
    asm volatile(
        "rep movsb"
        :
        : "D"(dest), "S"(src), "c"(n)
        : "memory"
    );
    return dest;
}

EFIAPI void* memset(void* s, int c, size_t n) {
    asm volatile(
        "rep stosb"
        :
        : "D"(s), "a"((uint8_t)c), "c"(n)
        : "memory"
    );
    return s;
}

EFIAPI int memcmp(const void* s1, const void* s2, size_t n) {
    int res = 0;

	asm volatile(
        "repe cmpsb\n\t"
        "je 1f\n\t"
        "movzbl -1(%%rsi), %%eax\n\t"
        "movzbl -1(%%rdi), %%edx\n\t"
        "sub %%edx, %%eax\n\t"
        "jmp 2f\n"
        "1:\n\t"
        "xor %%eax, %%eax\n"
        "2:"
        :
		"=&a"(res)
        :
		"S"(s1), "D"(s2), "c"(n)
        :
		"rdx", "memory"
    );

	return res;
}

EFIAPI size_t strlen(const char* s) {
    size_t len;

    asm volatile(
        "xor %%al, %%al\n\t"
        "mov $-1, %%rcx\n\t"
        "repne scasb\n\t"
        "not %%rcx\n\t"
        "dec %%rcx"
        :
		"=c"(len)
        :
		"D"(s)
        :
		"rax", "memory"
    );

    return len;
}

EFIAPI int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

EFIAPI char* strncpy(char* dest, const char* src, size_t n) {
    while (n && *src) {
		*dest++=*src++;
		n--;
	}

	while (n--) *dest++=0;
}

EFIAPI void ___chkstk_ms(void* size_in) {
	return; // No NEED TO BE honest
}

typedef struct {
	CHAR16 c;
	UINTN x;
	UINTN y;
	UINTN attr;
} vmem_c_buf;
vmem_c_buf buf[64];
uint8_t buf_ptr = 0;
uint64_t last_x = 0;
uint64_t last_y = 0;
UINTN last_attr = 0;

EFIAPI static void uefi_print_ex_integer(struct VMemDesign* design, uint64_t val, int base, int width, int zero_pad, int is_signed) {
    if (pefi_state.initialized != 1) return;

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

EFIAPI static void uefi_printvf_design(struct VMemDesign* design, const char* fmt, va_list args) {
	vmem_flush();
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
				case '\n': {
					vmem_printc(design, '\n');
					vmem_printc(design, '\r');
					break;
				}
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (!s) {
						s = "(NULL)";
					}

					vmem_print(design, s);
                    break;
                }
				case 'S': {
                    const CHAR16* s = va_arg(args, const CHAR16*);
                    pefi_print(pefi_state.system_table, s ? s : u"(NULL)");
                    break;
                }
                case 'i':
                case 'd': { // signed 32/64-bit
                    int64_t d;
                    if (is_long >= 1) d = va_arg(args, int64_t);
                    else d = (int64_t)va_arg(args, int);
                    uefi_print_ex_integer(design, (uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    uefi_print_ex_integer(design, u, 10, width, zero_pad, 0);
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
                    uefi_print_ex_integer(design, p, 16, width, zero_pad, 0);
                    break;
                }
                case '%': {
                    vmem_printc(design, *fmt);
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
	vmem_flush();
}

EFIAPI static void uefi_printvf(const char* fmt, va_list args) {
	struct VMemDesign design = {
		.fg = (enum VMemColors)(last_attr & 0xF),
		.bg = (enum VMemColors)(last_attr >> 4),
		.x = last_x,
		.y = last_y,
		.serial_out = 0
	};
	uefi_printvf_design(&design, fmt, args);
}

EFIAPI void uefi_printf(const char* fmt, ...) {
	if (pefi_state.initialized != 1) return;

    va_list args;
    va_start(args, fmt);

    uefi_printvf(fmt, args);

    va_end(args);
}

#define BCD_TO_BIN(bcd) (((bcd) & 0x0F) + (((bcd) >> 4) * 10))

static uint64_t tsc_ticks_per_ms = 0;
static uint8_t rdtscp_supported = 0;
static uint64_t bootup_timestamp = 0;

static const uint8_t month_days[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

EFIAPI static inline uint64_t ktimer_read_tsc(void) {
    uint32_t low = 0;
    uint32_t high = 0;
    if (rdtscp_supported == 1) {
        asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    }
    else {
        asm volatile("lfence\n\t" "rdtsc" : "=a"(low), "=d"(high) : : "memory");
    }
    return ((uint64_t)high << 32) | low;
}

EFIAPI static inline int is_leap_year(uint32_t year) {
    return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0)));
}

EFIAPI static uint64_t rtc_to_timestamp(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second) {
	uint64_t leaps = ((year-1)/4 - 1969/4) - ((year-1)/100 - 1969/100) + ((year-1)/400 - 1969/400);
    uint64_t days = ((year - 1970) * 365) + leaps;

    // months
    for (uint32_t m = 1; m < month; m++) {
        days += month_days[m - 1];
        if (m == 2 && is_leap_year(year)) days++;
    }
    days += day - 1;

    return days * 86400ULL + hour * 3600ULL + minute * 60ULL + second;
}

EFIAPI void ktimer_calibrate(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000001;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    if (edx & (1 << 27)) {
        rdtscp_supported = 1;
    } else {
        rdtscp_supported = 0;
    }

    uint64_t start = ktimer_read_tsc();
    pefi_state.system_table->BootServices->Stall(10000);
    uint64_t end = ktimer_read_tsc();
    uint64_t cycles_per_10ms = end - start;
    tsc_ticks_per_ms = cycles_per_10ms / 10;

	// Wait for RTC to finish updating
	uint64_t timeout = kget_ms_passed();
	for (; kget_ms_passed() - timeout < 10000;) {
		asm_outb(0x70, 0x0A);
		if (!(asm_inb(0x71) & 0x1)) break;
		asm volatile("pause" ::: "memory");
	}

	// Read CMOS RTC (0x00=s, 0x02=min, 0x04=hrs, 0x07=day, 0x08=month, 0x09=year, 0x32=century)
	asm_outb(0x70, 0x0B);
	uint8_t is_bin = (asm_inb(0x71) & 0x04) != 0;
	asm_outb(0x70, 0x00);
	uint8_t sec = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x02);
	uint8_t min = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x04);
	uint8_t hr = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x07);
	uint8_t day = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x08);
	uint8_t month = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x09);
	uint8_t year = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));
	asm_outb(0x70, 0x32);
	uint8_t century = is_bin ? asm_inb(0x71) : BCD_TO_BIN(asm_inb(0x71));

	if (century == 0) century = 20;

	uint32_t full_year = century * 100 + year;
	bootup_timestamp = rtc_to_timestamp(full_year, month, day, hr, min,sec);
}

EFIAPI void kdelay(uint32_t ms) {
    if (tsc_ticks_per_ms == 0 || pefi_state.initialized != 1) return;

    uint64_t start = ktimer_read_tsc();
    uint64_t ticks_needed = (uint64_t)ms * tsc_ticks_per_ms;

    while ((ktimer_read_tsc() - start) < ticks_needed) {
        asm volatile("pause" ::: "memory");
    }
}

EFIAPI uint64_t kget_ms_passed(void) {
    if (tsc_ticks_per_ms == 0) return 0;

    return (uint64_t)(ktimer_read_tsc() / tsc_ticks_per_ms);
}

EFIAPI uint64_t kget_timestamp_seconds(void) {
	return bootup_timestamp + (kget_ms_passed() / 1000);
}

EFIAPI uint64_t kget_timestamp_ms(void) {
	return (bootup_timestamp * 1000) + kget_ms_passed();
}

EFIAPI void vmem_flush(void) {
    if (pefi_state.initialized != 1 || buf_ptr == 0) return;

    CHAR16 strbuf[128];
    uint8_t i = 0;

    while (i < buf_ptr) {
        vmem_c_buf *first = &buf[i];

        UINTN attr = first->attr;
        UINTN x = first->x;
        UINTN y = first->y;

        uint32_t len = 0;

        while (i < buf_ptr && len < (sizeof(strbuf)/sizeof(CHAR16))-1) {
            vmem_c_buf *b = &buf[i];
            if (b->attr != attr) break;
            if (b->y != y) break;
            if (b->x != (x + len)) break;

            strbuf[len++] = b->c;
            i++;
        }

        strbuf[len] = 0;

        if (last_attr != attr) {
            pefi_state.system_table->ConOut->SetAttribute(pefi_state.system_table->ConOut, attr);
            last_attr = attr;
        }

        if (last_x != x || last_y != y) {
            vmem_set_cursor(x, y);
            last_x = x;
            last_y = y;
        }

        pefi_print(pefi_state.system_table, strbuf);
        last_x += len;
    }

    buf_ptr = 0;
	memset(buf, 0, sizeof(buf));
}

EFIAPI void vmem_printc(struct VMemDesign* design, char c) {
	if (pefi_state.initialized != 1) return;
    UINTN attr = ((UINTN)design->bg << 4) | (UINTN)design->fg;
    if (c == '\n') {
        design->x = 0;
        design->y++;
        return;
    }
    
	if (buf_ptr >= 64) vmem_flush();
	vmem_c_buf* b = &buf[buf_ptr++];
	b->c = (CHAR16)c;
	b->x = design->x;
	b->y = design->y;
	b->attr = attr;

    design->x++;
}

EFIAPI void vmem_printwc(struct VMemDesign* design, CHAR16* c) {
	if (pefi_state.initialized != 1) return;
    UINTN attr = ((UINTN)design->bg << 4) | (UINTN)design->fg;
    if (c == (CHAR16)'\n') {
        design->x = 0;
        design->y++;
        return;
    }
    
	if (buf_ptr >= 64) vmem_flush();
	vmem_c_buf* b = &buf[buf_ptr++];
	b->c = *c;
	b->x = design->x;
	b->y = design->y;
	b->attr = attr;

    design->x++;
}

EFIAPI void vmem_print(struct VMemDesign* design, const char* str) {
    if (pefi_state.initialized != 1) return;

	vmem_flush();
	
	UINTN attr = ((UINTN)design->bg << 4) | (UINTN)design->fg;
	if (last_attr != attr) {
		pefi_state.system_table->ConOut->SetAttribute(pefi_state.system_table->ConOut, attr);
		last_attr = attr;
	}
	if (last_x != design->x || last_y != design->y) {
		vmem_set_cursor(design->x, design->y);
		last_x = design->x;
		last_y = design->y;
	}

	CHAR16 wc[strlen(str)*2+1];
	memset(wc, 0, sizeof(wc));
	size_t i = 0;
	for (char* p = (char*)str; *p; p++) {
		switch(*p) {
			case '\n': {
				wc[i++] = (CHAR16)'\n';
				wc[i++] = (CHAR16)'\r';
				design->y++;
				design->x = 0;
				continue;
			}
			default: {
				design->x++;
				break;
			}
		}
		wc[i++] = (CHAR16)(*p);
	}
	wc[i++] = '\0';
	pefi_print(pefi_state.system_table, wc);
}

EFIAPI void vmem_clear_screen(struct VMemDesign* design) {
	UINTN attr = ((UINTN)(design)->bg << 4) | (UINTN)(design)->fg;
	if (last_attr != attr) {
		pefi_state.system_table->ConOut->SetAttribute(pefi_state.system_table->ConOut, attr);
		last_attr = attr;
	}
	pefi_state.system_table->ConOut->ClearScreen(pefi_state.system_table->ConOut);
	design->x = 0;
	design->y = 0;
	if (last_x != 0 || last_y != 0) {
		vmem_set_cursor(0, 0);
		last_x = 0;
		last_y = 0;
	}
}

EFIAPI void vmem_printf(struct VMemDesign* design, const char* fmt, ...) {
	vmem_flush();
	
    va_list args;
    va_start(args, fmt);

	uefi_printvf_design(design, fmt, args);
    
    va_end(args);
}

