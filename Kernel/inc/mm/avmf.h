#pragma once
// AOS Virtual Memory Format
#include <inttypes.h>

#define AVMF_VERSION 1
#define AVMF_SIGNATURE (uint32_t)0xA1322F // A, V (13th letter), M (22nd letter), F

typedef struct AVMF_Header {
    uint32_t signature; // AVMF
    uint16_t version;
    uint64_t virt_addr;
    uint64_t phys_addr;
    uint64_t size;
    uint8_t used;
    uint32_t flags;
    uint32_t attributes;
    struct AVMF_Header* next;
} avmf_header_t;

#define AVMF_FLAG_PRESENT (1 << 0)
#define AVMF_FLAG_WRITEABLE (1 << 1)
#define AVMF_FLAG_EXECUTABLE (1 << 2)
#define AVMF_FLAG_USERMODE (1 << 3)
#define AVMF_FLAG_KERNEL (1 << 4)

#define AVMF_ATTR_CACHED (1 << 0)
#define AVMF_ATTR_SHARED (1 << 1)
#define AVMF_ATTR_LOCKED (1 << 2) // not swappable

uint64_t avmf_virt_to_phys(uint64_t virt) __attribute__((used));
void* avmf_phys_to_virt(uint64_t phys) __attribute__((used));
void* avmf_map_phys_to_virt(uint64_t phys, uint64_t size, int flags) __attribute__((used));
uint64_t avmf_alloc_phys_contiguous(uint64_t size) __attribute__((used));
void avmf_init(uint64_t base_virt, uint64_t region_size) __attribute__((used));
uint64_t avmf_alloc_region(uint64_t size, uint32_t flags) __attribute__((used));
int avmf_map(uint64_t virt, uint64_t phys, uint64_t size, uint32_t flags) __attribute__((used));
avmf_header_t* avmf_find(uint64_t virt);
void avmf__debug__print_map() __attribute__((used));
