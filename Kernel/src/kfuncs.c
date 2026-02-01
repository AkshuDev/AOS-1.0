#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <stddef.h>

#include <inc/kfuncs.h>
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

// Allocators
struct malloc_hdr {
    char signature[4]; // 'AOSM'
    uint64_t size;
    uint64_t phys;
    uint8_t is_free;
    struct malloc_hdr* next;
    struct malloc_hdr* prev;
} __attribute__((packed));

static struct malloc_hdr* heap_user_start = NULL;
static struct malloc_hdr* heap_user_end = NULL;
static struct malloc_hdr* heap_kernel_start = NULL;
static struct malloc_hdr* heap_kernel_end = NULL;
static struct malloc_hdr* heap_driver_start = NULL;
static struct malloc_hdr* heap_driver_end = NULL;
static struct malloc_hdr* heap_sensitive_start = NULL;
static struct malloc_hdr* heap_sensitive_end = NULL;
static uintptr_t heap_user_break = AOS_USER_SPACE_BASE;
static uintptr_t heap_kernel_break = AOS_KERNEL_SPACE_BASE;
static uintptr_t heap_driver_break = AOS_DRIVER_SPACE_BASE;
static uintptr_t heap_sensitive_break = AOS_SENSITIVE_SPACE_BASE;

static void link_alloc(struct malloc_hdr* hdr, MemoryAllocType type) {
    struct malloc_hdr** end = &heap_user_end;
    switch (type) {
        case MALLOC_TYPE_USER:
            break;
        case MALLOC_TYPE_KERNEL:
            end = &heap_kernel_end; break;
        case MALLOC_TYPE_DRIVER:
            end = &heap_driver_end; break;
        case MALLOC_TYPE_SENSITIVE:
            end = &heap_sensitive_end; break;
        default: return;
    }
    struct malloc_hdr* cur = *end;
    while (cur->next) {
        cur = cur->next;
    }
    hdr->next = NULL;
    hdr->prev = cur;
    cur->next = hdr;
    *end = hdr;
}

void* memory_ralloc(size_t size, uint64_t phys, MemoryAllocType type) { // RAW Alloc
    // TODO: Make
    return NULL;
}

void* memory_aalloc(size_t size, uint64_t phys, size_t alignment, MemoryAllocType type) { // ALIGNED Alloc
    size_t total_size = size + alignment + sizeof(void*);
    void* raw = memory_ralloc(total_size, phys, type);
    if (!raw) return NULL;

    uintptr_t addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned = (addr + (alignment - 1)) & ~(alignment - 1);

    return (void*)aligned;
}

void* memory_alloc(size_t size, uint64_t phys, MemoryAllocType type) { // Alloc (Aligned to ^2)
    return memory_aalloc(size, phys, 2, type);
}

