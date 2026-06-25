#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>

#include <stddef.h>
#include <limits.h>

#include <inc/core/acpi.h>
#include <inc/core/kfuncs.h>

#include <inc/drivers/io/io.h>

#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>

// Memory and Stuff
void* memset(void* s, int c, size_t n) {
    asm volatile(
        "rep stosb"
        :
        : "D"(s), "a"((uint8_t)c), "c"(n)
        : "memory"
    );
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
	asm volatile(
        "rep movsb"
        :
        : "D"(dest), "S"(src), "c"(n)
        : "memory"
    );
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
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

int strcmp(char* s1, char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(char* s1, char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

size_t strlen(char* s) {
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

char* strcpy(char* dest, char* src) {
    char *ret=dest;
    while((*dest++=*src++)) ;
    return ret;
}

char* strncpy(char* dest, char* src, size_t n) {
	while (n && *src) {
		*dest++=*src++;
		n--;
	}

	while (n--) *dest++=0;
}

uint32_t str_to_uint(const char* str) {
    uint32_t result = 0;
    if (!str) return 0;

    // Hex prefix?
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        while (*str) {
            char c = *str++;
            result <<= 4;
            if (c >= '0' && c <= '9') result += c - '0';
            else if (c >= 'a' && c <= 'f') result += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') result += c - 'A' + 10;
            else break; // stop at first invalid char
        }
    } else {
        while (*str >= '0' && *str <= '9') {
            result = result * 10 + (*str - '0');
            str++;
        }
    }

    return result;
}

void spin_lock(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock);
    }
}

void spin_unlock(spinlock_t* lock) {
    __sync_lock_release(lock);
}

uint64_t spin_lock_irqsave(spinlock_t* lock) {
    uint64_t flags = 0;
    asm volatile(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        :
        "=r"(flags)
        : :
        "memory"
    );
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) {
            asm volatile("pause" ::: "memory"); // "Performance is Key" - Some random dude sitting on a chair programming this
        }
    }

    return flags;
}

void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags) {
    __sync_lock_release(lock);
    asm volatile(
        "push %0\n\t"
        "popfq"
        : :
        "r"(flags)
        :
        "memory", "cc"
    );
}

#define BCD_TO_BIN(bcd) (((bcd) & 0x0F) + (((bcd) >> 4) * 10))

static uint64_t tsc_ticks_per_ms = 0;
static aos_bool rdtscp_supported = 0;
static uint64_t bootup_timestamp = 0;

static const uint8_t month_days[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

static inline uint64_t ktimer_read_tsc(void) {
    uint32_t low = 0;
    uint32_t high = 0;
    if (rdtscp_supported) {
        asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    }
    else {
        asm volatile("lfence\n\t" "rdtsc" : "=a"(low), "=d"(high) : : "memory");
    }
    return ((uint64_t)high << 32) | low;
}

static inline int is_leap_year(uint32_t year) {
    return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0)));
}

static uint64_t rtc_to_timestamp(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second) {
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

void ktimer_calibrate(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000001;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    if (edx & (1 << 27)) {
        rdtscp_supported = AOS_TRUE;
    } else {
        rdtscp_supported = AOS_FALSE;
    }

    uint64_t start = ktimer_read_tsc();
    acpi_mdelay(10);
    uint64_t end = ktimer_read_tsc();
    uint64_t cycles_per_10ms = end - start;
    tsc_ticks_per_ms = cycles_per_10ms / 10;
    aos_sysinfo_t* sysinfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    sysinfo->tsc_freq_hz = tsc_ticks_per_ms * 1000;

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

	serial_printf("Bootup Timestamp: %u\n", bootup_timestamp);
}

void kdelay(uint32_t ms) {
    if (tsc_ticks_per_ms == 0) return;

    uint64_t start = ktimer_read_tsc();
    uint64_t ticks_needed = (uint64_t)ms * tsc_ticks_per_ms;

    while ((ktimer_read_tsc() - start) < ticks_needed) {
        asm volatile("pause" ::: "memory");
    }
}

uint64_t kget_ms_passed(void) {
    if (tsc_ticks_per_ms == 0) return 0;

    return (uint64_t)(ktimer_read_tsc() / tsc_ticks_per_ms);
}

uint64_t kget_timestamp_seconds(void) {
	return bootup_timestamp + (kget_ms_passed() / 1000);
}

uint64_t kget_timestamp_ms(void) {
	return (bootup_timestamp * 1000) + kget_ms_passed();
}

// Extras

uint64_t kcompute_checksum(const uint8_t* data, uint32_t len) {
    uint64_t sum = 0;
    for (uint64_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

#define ALLOC_HDR_SIG "AOS_ALLOC\0"
#define ALLOC_HDR_SIG_LEN 10

struct alloc_hdr {
    char sig[ALLOC_HDR_SIG_LEN];
    size_t size;
    struct alloc_hdr* nxt;
    struct alloc_hdr* prev;
};

void* kmalloc(size_t size) {
    void* out = (void*)avmf_alloc(size + sizeof(struct alloc_hdr), MALLOC_TYPE_USER, PAGE_PRESENT | PAGE_RW, NULL);
    if (!out) return out;
    void* ret = out + sizeof(struct alloc_hdr);
    
    struct alloc_hdr* hdr = (struct alloc_hdr*)out;
    
    memcpy(hdr->sig, ALLOC_HDR_SIG, ALLOC_HDR_SIG_LEN);
    hdr->size = size;
    hdr->nxt = NULL;
    hdr->prev = NULL;

    return ret;
}

void klink(void* ptr1, void* ptr2) {
    struct alloc_hdr* hdr1 = (struct alloc_hdr*)(ptr1 - sizeof(struct alloc_hdr));
    struct alloc_hdr* hdr2 = (struct alloc_hdr*)(ptr2 - sizeof(struct alloc_hdr));
    
    hdr1->nxt = hdr2;
    hdr2->prev = hdr1;
}

void kfree(void* ptr) {
    struct alloc_hdr* hdr = (struct alloc_hdr*)(ptr - sizeof(struct alloc_hdr));
    while (hdr != NULL) {
        if (memcmp(hdr->sig, ALLOC_HDR_SIG, ALLOC_HDR_SIG_LEN) != 0) break;

        uint64_t free_ptr = (uint64_t)hdr;
        hdr = hdr->nxt;
        avmf_free(free_ptr);
    }
}

void* kcalloc(size_t nmemb, size_t size) {
    void* out = kmalloc(nmemb * size);
    if (!out) return out;
    memset(out, 0, size * nmemb);
    return out;
}

void* krealloc(void* ptr, size_t new_size) {
	if (!ptr) {
		return kmalloc(new_size);
	}
    struct alloc_hdr* hdr = (struct alloc_hdr*)(ptr - sizeof(struct alloc_hdr));
    if (memcmp(hdr, ALLOC_HDR_SIG, ALLOC_HDR_SIG_LEN) != 0) return NULL;
    
    void* out = kmalloc(new_size);
    if (!out) return out;

    memcpy(out, ptr, hdr->size);
    kfree(ptr);
    return out;
}

// System info
static aos_sysinfo_t* system_info;
static aos_bool sysinfo_checked = AOS_FALSE;

aos_sysinfo_t* kget_sysinfo(void) {
	if (!sysinfo_checked) {
		aos_sysinfo_t* sinfo = (aos_sysinfo_t*)(AOS_SYS_INFO_ADDR);
		aos_sysinfo_t info = *sinfo;
		
		info.checksum = 0;
		uint64_t checksum_comp = kcompute_checksum((const uint8_t*)&info, sizeof(aos_sysinfo_t));

		uint8_t out = (uint8_t)(checksum_comp == sinfo->checksum);

		serial_printf("[KFUNCS] Computed Checksum for SystemInfo = %llu\n\tProvided Checksum = %llu\n", checksum_comp, sinfo->checksum);

		if (!out) system_info = NULL;
		else system_info = sinfo;

		sysinfo_checked = AOS_TRUE;
	}
	return system_info;
}

// Pheonix STDLIB Functions

aos_bool kc_is_alpha(char c) {
    c |= 0x20;
    return (c >= 'a' && c <= 'z');
}

aos_bool kc_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

aos_bool kc_is_alphanum(char c) {
    return kc_is_alpha(c) || kc_is_digit(c);
}

aos_bool kis_alpha(char* s) {
    for (char* p = s; *p; p++) {
        if (!kc_is_alpha(*p)) return AOS_FALSE;
    }
    return AOS_TRUE;
}

aos_bool kis_digit(char* s) {
    for (char* p = s; *p; p++) {
        if (!kc_is_digit(*p)) return AOS_FALSE;
    }
    return AOS_TRUE;
}

aos_bool kis_alphanum(char* s) {
    for (char* p = s; *p; p++) {
        if (!kc_is_alphanum(*p)) return AOS_FALSE;
    }
    return AOS_TRUE;
}

aos_bool kis_float(char* s) {
    if (!s || !*s) return 0;

    aos_bool has_digit = AOS_FALSE;
    aos_bool has_dot = AOS_FALSE;

    if (*s == '+' || *s == '-') s++;

    while (*s) {
        if (kc_is_digit(*s)) {
            has_digit = AOS_TRUE;
        } else if (*s == '.' && !has_dot) {
            has_dot = AOS_TRUE;
        } else {
            return AOS_FALSE;
        }
        s++;
    }

    return has_digit;
}

int kchar_to_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;

    return -1;
}

uint64_t kstr_to_u64(const char* str, int base) {
    if (!str || base < 2 || base > 36) return 0;

    uint64_t result = 0;

    if (*str == '+') str++;

    while (*str) {
        int digit = kchar_to_digit(*str);
        if (digit < 0 || digit >= base)
            break;

        if (result > (UINT64_MAX - digit) / base) {
            return UINT64_MAX;
        }

        result = result * base + digit;
        str++;
    }

    return result;
}

int64_t kstr_to_i64(const char* str, int base) {
    if (!str || base < 2 || base > 36) return 0;

    aos_bool neg = AOS_FALSE;

    if (*str == '-') {
        neg = AOS_TRUE;
        str++;
    } else if (*str == '+') {
        str++;
    }

    uint64_t result = 0;

    while (*str) {
        int digit = kchar_to_digit(*str);
        if (digit < 0 || digit >= base)
            break;

        if (result > (UINT64_MAX - digit) / base) {
            return neg ? INT64_MIN : INT64_MAX;
        }

        result = result * base + digit;
        str++;
    }

    if (neg) {
        if (result > (uint64_t)INT64_MAX + 1)
            return INT64_MIN;
        return -(int64_t)result;
    }

    if (result > (uint64_t)INT64_MAX)
        return INT64_MAX;

    return (int64_t)result;
}

double kstr_to_double(const char* str) {
    if (!str) return 0.0;

    double result = 0.0;
    double frac = 0.0;
    double div = 1.0;
    int sign = 1;

    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }

    while (kc_is_digit(*str)) {
        result = result * 10.0 + (*str - '0');
        str++;
    }

    if (*str == '.') {
        str++;
        while (kc_is_digit(*str)) {
            frac = frac * 10.0 + (*str - '0');
            div *= 10.0;
            str++;
        }
        result += frac / div;
    }

    return result * sign;
}
