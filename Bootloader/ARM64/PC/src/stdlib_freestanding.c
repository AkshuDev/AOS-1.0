#include <aos_inttypes.h>

#include <stddef.h>
#include <stdarg.h>

#include <system.h>

#include <pefi.h>
#include <pefilib.h>
#include <pefi_simple_text_out.h>
#include <pefi_simple_text_in.h>

#include <freestanding.h>

EFIAPI void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dest;
}

EFIAPI void* memset(void* s, int c, size_t n) {
	uint8_t* d = s;
    while (n--) *d++ = (uint8_t)c;
    return s;
}

EFIAPI int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* x = s1;
    const uint8_t* y = s2;

    while (n--) {
        if (*x != *y) return *x - *y;
        x++;
        y++;
    }

    return 0;
}

EFIAPI size_t strlen(const char* s) {
    const char* p = s;
    while (*p) p++;
    return p - s;
}

EFIAPI int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

EFIAPI char* strncpy(char* dest, const char* src, size_t n) {
	char* dstart = dest;

    while (n && *src) {
		*dest++=*src++;
		n--;
	}

	while (n--) *dest++=0;
	return dstart;
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

static uint64_t ticks_per_ms = 0;
static uint64_t bootup_timestamp = 0;

static const uint8_t month_days[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

EFIAPI static inline uint64_t kread_cntvct(void) {
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline uint64_t ktimer_frequency(void) {
    uint64_t f;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
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
    ticks_per_ms = ktimer_frequency() / 1000;

	if (!pefi_state.initialized) return;

	EFI_TIME time;
	pefi_state.runtime_services->GetTime(&time, NULL);
	bootup_timestamp = rtc_to_timestamp(time.Year, time.Month, time.Day, time.Hour, time.Minute, time.Second);
}

EFIAPI void kdelay(uint32_t ms) {
    if (ticks_per_ms == 0 || pefi_state.initialized != 1) return;

    uint64_t start = kread_cntvct();
    uint64_t ticks_needed = (uint64_t)ms * ticks_per_ms;

    while ((kread_cntvct() - start) < ticks_needed) {
        asm volatile("yield");
    }
}

EFIAPI uint64_t kget_ms_passed(void) {
    if (ticks_per_ms == 0) return 0;

    return (uint64_t)(kread_cntvct() / ticks_per_ms);
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

