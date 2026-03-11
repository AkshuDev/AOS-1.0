#include <stddef.h>
#include <stdint.h>

#include <pefi.h>

EFIAPI void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

EFIAPI void* memset(void* s, int c, size_t n) {
    uint8_t* p = s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

EFIAPI int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* a = s1, *b = s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

EFIAPI size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

EFIAPI int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return *(unsigned char*)a - *(unsigned char*)b;
}

EFIAPI char* strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

#define STACK_PAGE_SIZE 0x1000

EFIAPI void ___chkstk_ms(void* size_in) {
    uintptr_t size = (uintptr_t)size_in;
    if (size == 0) return;

    uintptr_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    uintptr_t new_rsp = rsp - size;

    // Probe each 4 KB page
    while (rsp > new_rsp) {
        rsp -= STACK_PAGE_SIZE;
        // Touch the stack to force page commit
        volatile char tmp = *((volatile char*)rsp);
        (void)tmp;
    }
}