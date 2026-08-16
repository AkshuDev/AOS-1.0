#pragma once
// AOS Virtual Memory Format
#include <aos_inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>

#define AVMF_VERSION 1
#define AVMF_SIGNATURE (uint32_t)0xA1322F // A, V (13th letter), M (22nd letter), F

typedef enum AVMF_HeaderType {
	AVMF_HDR_TYPE_CORRUPT = 0,
	AVMF_HDR_TYPE_CACHE,
	AVMF_HDR_TYPE_ALLOC
} avmf_header_type_t;

typedef struct AVMF_Header {
    uint32_t signature; // AVMF
    uint16_t version;
	
	enum AVMF_HeaderType type;

    uint64_t virt_addr;
    uint64_t phys_addr;

    uint64_t size;

    aos_bool used;
    uint32_t flags;
    uint32_t attributes;

    struct AVMF_Header* next;
	struct AVMF_Header* parent;
} avmf_header_t;

typedef struct AVMF_Range {
    uint64_t base;
    uint64_t size;
    struct AVMF_Range* next;
} avmf_range_t;

typedef struct AVMF_Region_Header {
    uint32_t signature; // AVMF
    uint16_t version;
    
    uint64_t base;
    uint64_t limit;

	uint64_t allocated_bytes;

	spinlock_t lock;

    struct AVMF_Range* free_list;
	uint64_t free_count;
	struct AVMF_Range* free_list_end;

	struct AVMF_Header* alloc_list;
	uint64_t alloc_count;
	struct AVMF_Header* alloc_list_end;

    struct AVMF_Header* cache_list;
	uint64_t cache_count;
	struct AVMF_Header* cache_list_end;
} avmf_region_header_t;

typedef enum {
    MALLOC_TYPE_UNKNOWN = 0,
    MALLOC_TYPE_USER,
    MALLOC_TYPE_KERNEL,
    MALLOC_TYPE_DRIVER,
    MALLOC_TYPE_SENSITIVE
} MemoryAllocType;

#define AVMF_FLAG_READABLE (1 << 0)
#define AVMF_FLAG_WRITEABLE (1 << 1)
#define AVMF_FLAG_RW (AVMF_FLAG_READABLE | AVMF_FLAG_WRITEABLE)
#define AVMF_FLAG_EXECUTABLE (1 << 2)
#define AVMF_FLAG_USERMODE (1 << 3)
#define AVMF_FLAG_NO_CACHE (1 << 4)
#define AVMF_FLAG_GLOBAL (1 << 5)
#define AVMF_FLAG_DIRTY (1 << 6)

#define AVMF_ATTR_CACHED (1 << 0)
#define AVMF_ATTR_SHARED (1 << 1)
#define AVMF_ATTR_LOCKED (1 << 2) // not swappable

uint64_t avmf_alloc_phys_contiguous(uint64_t size) __attribute__((used));
void avmf_free_phys_contiguous(uint64_t phys, uint64_t size) __attribute__((used));

void avmf_init(uint64_t* base_phys, uint64_t* limit_phys, uint8_t entries) __attribute__((used));

uint64_t avmf_alloc_virt(uint64_t size, MemoryAllocType type) __attribute__((used));
uint64_t avmf_alloc(uint64_t size, MemoryAllocType type, uint32_t flags, uint64_t* phys_out) __attribute__((used));
void avmf_free(uint64_t virt) __attribute__((used));

avmf_header_t* avmf_find(uint64_t virt) __attribute__((used));

void avmf_print_info(aos_bool vmem, struct VMemDesign* design) __attribute__((used));