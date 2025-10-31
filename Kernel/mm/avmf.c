#include <inttypes.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <stddef.h>

#include <system.h>

#define __log(msg, ...) // stubbed
#define PAGE_SIZE 0x1000

static avmf_header_t* avmf_head;
static uint64_t avmf_base = 0;
static uint64_t avmf_limit = 0;

static inline uint64_t align4k(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

#define INITIAL_FREE_PHYS align4k(AOS_KERNEL_ADDR + AOS_KERNEL_SIZE)
#define END_FREE_PHYS INITIAL_FREE_PHYS + 0x200000
static uint64_t next_free_phys;
static uintptr_t next_free_page;

static inline uint64_t avmf_alloc_phys_page(void) {
    if (next_free_page + PAGE_SIZE > END_FREE_PHYS) {
        serial_print("[AVMF] Out of physical memory!\n");
        return 0;
    }
    uint64_t page = next_free_page;
    next_free_page += PAGE_SIZE;
    return page;
}

uint64_t avmf_virt_to_phys(uint64_t virt) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) return 0; // Unmapped region
    uint64_t offset = virt - region->virt_addr;
    return region->phys_addr + offset;
}

void* avmf_phys_to_virt(uint64_t phys) {
    avmf_header_t* cur = avmf_head;
    while (cur) {
        if (cur->phys_addr <= phys && phys < cur->phys_addr + cur->size) {
            return (void*)(cur->virt_addr + (phys - cur->phys_addr));
        }
        cur = cur->next;
    }
    return NULL;
}

uint64_t avmf_alloc_phys_contiguous(uint64_t size) {
    // Align size to page
    uint64_t npages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t base = 0;
    for (uint64_t i = 0; i < npages; i++) {
        uint64_t page = avmf_alloc_phys_page();
        if (i == 0) base = page;
        else {
            // Ensure contiguous
            if (page != base + i * PAGE_SIZE) {
                serial_print("[AVMF] Memory not contiguous!\n");
                return 0;
            }
        }
    }
    return base;
}

void* avmf_map_phys_to_virt(uint64_t phys, uint64_t size, int flags) {
    void* virt = avmf_alloc_region(size, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);
    pager_map_range((uint64_t)virt, phys, size, flags);
    return virt;
}

void avmf_init(uint64_t base_virt, uint64_t region_size) {
    avmf_base = align4k(base_virt);
    avmf_limit = avmf_base + align4k(region_size);
    avmf_head = (avmf_header_t*)NULL;
    next_free_phys = (uint64_t)INITIAL_FREE_PHYS;
    next_free_page = (uintptr_t)INITIAL_FREE_PHYS;
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
    node->phys_addr = next_free_phys;
    next_free_phys += size;
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
