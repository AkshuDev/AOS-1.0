#include <inttypes.h>
#include <asm.h>

#include <stddef.h>

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