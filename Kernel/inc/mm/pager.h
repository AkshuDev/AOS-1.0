#pragma once
#include <inttypes.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGE_PRESENT 0x1 // Page Present
#define PAGE_RW 0x2 // Read/Write
#define PAGE_USER 0x4 // User-mode accessable
#define PAGE_PWT 0x8 // Write through
#define PAGE_PCD 0x10 // Cache-disable
#define PAGE_ACCESSED 0x20 // Accessed
#define PAGE_DIRTY 0x40 // Dirty
#define PAGE_PAT 0x80 // Page Attribute Table
#define PAGE_GLOBAL 0x100 // Global
#define PAGE_XD 0x8000000000000000ULL // EXEC DISABLE

typedef uint64_t page_entry_t;
typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

struct page_table {
    page_entry_t entries[64];
} __attribute__((aligned(4096)));

void pager_init(uint64_t fb_phys, uint64_t fb_size) __attribute__((used));
struct page_table* pager_map(virt_addr_t virt, phys_addr_t phys, uint64_t flags) __attribute__((used));
void pager_load(struct page_table* pml4) __attribute__((used));
