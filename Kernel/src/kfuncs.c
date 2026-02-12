#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <stddef.h>

#include <inc/core/acpi.h>
#include <inc/core/kfuncs.h>
#include <inc/mm/pager.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// Memory and Stuff
void* memset(void* s, int c, size_t n) {
    uint8_t* p = s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = s1;
    const uint8_t* p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
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
    asm volatile("pushfq ; pop %0 ; cli" : "=rm"(flags) : : "memory");
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock);
    }
}

void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags) {
    __sync_lock_release(lock);
    asm volatile("push %0 ; popfq" : : "rm"(flags) : "memory", "cc");
}

static uint64_t tsc_ticks_per_ms = 0;

static uint64_t ktimer_read_tsc(void) {
    uint32_t low = 0;
    uint32_t high = 0;
    asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    return ((uint64_t)high << 32) | low;
}

void ktimer_calibrate(void) {
    uint64_t start = ktimer_read_tsc();
    acpi_mdelay(10);
    uint64_t end = ktimer_read_tsc();
    uint64_t cycles_per_10ms = end - start;
    tsc_ticks_per_ms = cycles_per_10ms / 10;
    aos_sysinfo_t* sysinfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    sysinfo->tsc_freq_hz = tsc_ticks_per_ms * 1000;
}

void kdelay(uint32_t ms) {
    if (tsc_ticks_per_ms == 0) return;

    uint64_t start = ktimer_read_tsc();
    uint64_t ticks_needed = (uint64_t)ms * tsc_ticks_per_ms;

    while ((ktimer_read_tsc() - start) < ticks_needed) {
        asm volatile("pause");
    }
}
