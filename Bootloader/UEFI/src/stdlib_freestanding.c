#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <pefi.h>
#include <pefilib.h>

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

static EFIAPI void uefi_print_ex_integer(uint64_t val, int base, int width, int zero_pad, int is_signed) {
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
                pefi_print(pefi_state.system_table, u"-");
                neg = 0;
            }
            while (padding_count--) pefi_print(pefi_state.system_table, u"0");
        } else {
            while(padding_count--) pefi_print(pefi_state.system_table, u" ");
        }
    }

    if (neg) pefi_print(pefi_state.system_table, u"-");
    while (i > 0) {
		CHAR16 w[2];
		w[0] = (CHAR16)(unsigned char)buf[--i];
		w[1] = 0;
        pefi_print(pefi_state.system_table, (const CHAR16*)w);
    }
}

EFIAPI void uefi_printf(const char* fmt, ...) {
	if (pefi_state.initialized != 1) return;

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
					CHAR16 w[2];
					w[0] = (CHAR16)(unsigned char)c;
					w[1] = 0;
                    pefi_print(pefi_state.system_table, (const CHAR16*)w);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (!s) {
						pefi_print(pefi_state.system_table, u"(NULL)");
						break;
					}

					CHAR16 wbuf[256];
					size_t i;

					for (i = 0; s[i] && i < 255; i++) wbuf[i] = (CHAR16)(unsigned char)s[i];
					wbuf[i] = 0;

					pefi_print(pefi_state.system_table, (const CHAR16*)wbuf);

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
                    uefi_print_ex_integer((uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    uefi_print_ex_integer(u, 10, width, zero_pad, 0);
                    break;
                }
                case 'x':
                case 'p': { // Pointer
                    uint64_t p;
                    if (*fmt == 'p') {
                        p = (uintptr_t)va_arg(args, void*);
                        pefi_print(pefi_state.system_table, u"0x");
                        if (width == 0) width = 16;
                        zero_pad = 1;
                    } else {
                        if (is_long >= 1) p = va_arg(args, uint64_t);
                        else p = (uint64_t)va_arg(args, uint32_t);
                    }
                    uefi_print_ex_integer(p, 16, width, zero_pad, 0);
                    break;
                }
                case '%': {
                    pefi_print(pefi_state.system_table, u"%");
                    break;
                }
                default: {
					CHAR16 w[2];
					w[0] = (CHAR16)(unsigned char)*fmt;
					w[1] = 0;
                    pefi_print(pefi_state.system_table, (const CHAR16*)w);
                    break;
                }
            }
        } else {
            CHAR16 w[2];
			w[0] = (CHAR16)(unsigned char)*fmt;
			w[1] = 0;
			pefi_print(pefi_state.system_table, (const CHAR16*)w);
        }
        fmt++;
    }

    va_end(args);
}
