#include <inttypes.h>
#include <inc/mm/avmf.h>

#include <stddef.h>

#define __log(msg, ...) // stubbed
#define PAGE_SIZE 0x1000

static avmf_header_t* avmf_head;
static uint64_t avmf_base = 0;
static uint64_t avmf_limit = 0;

static inline uint64_t align4k(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void avmf_init(uint64_t base_virt, uint64_t region_size) {
    avmf_base = align4k(base_virt);
    avmf_limit = avmf_base + align4k(region_size);
    avmf_head = (avmf_header_t*)NULL;
}

uint64_t avmf_alloc_region(uint64_t size, uint32_t flags) {
    size = align4k(size);
    uint64_t addr = avmf_base;
    avmf_header_t* current = avmf_head;

    // find the end of the current alloc list
    while (current && current->next) {
        addr = current->virt_addr + current->size;
        current = current->next;
    }

    // check bounds
    if (addr + size > avmf_limit) return 0;

    avmf_header_t* node = (avmf_header_t*)NULL;

    static avmf_header_t static_headers[64];
    static int header_index = 0;
    if (header_index >= 64) return 0;
    node = &static_headers[header_index++];
    
    node->virt_addr = addr;
    node->phys_addr = 0;
    node->size = size;
    node->flags = flags;
    node->used = 1;
    node->next = (avmf_header_t*)NULL;
    node->attributes = 0;
    node->version = AVMF_VERSION;
    node->signature = AVMF_SIGNATURE;

    if (!avmf_head) {
        avmf_head = node;
    } else {
        current->next = node;
    }

    return addr;
}

int avmf_map(uint64_t virt, uint64_t phys, uint64_t size, uint32_t flags) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) return -1;

    region->phys_addr = phys;
    region->flags |= flags;
    return 0;
}

avmf_header_t* avmf_find(uint64_t virt) {
    avmf_header_t* cur = avmf_head;
    while (cur) {
        if (virt >= cur->virt_addr && virt < cur->virt_addr + cur->size) {
            return cur;
        }
        cur = cur->next;
    }
    return (avmf_header_t*)NULL;
}

void avmf__debug_print_map(void) {
    avmf_header_t* cur = avmf_head;
    while (cur) {
        __log("AVMF: VIRT=%llx PHYS=%llx SIZE=%llu FLAGS=%x\n", cur->virt_addr, cur->phys_addr, cur->size, cur->flags);
        cur = cur->next;
    }
}
