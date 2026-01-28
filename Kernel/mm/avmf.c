#include <inttypes.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/io.h>

#include <stddef.h>

#include <system.h>

#define __log(msg, ...) // stubbed
#define PAGE_SIZE 0x1000

static avmf_header_t* avmf_head;
static avmf_header_t* last_found;

static uint64_t avmf_limit;
static uint64_t avmf_base;

static inline uint64_t align4k(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

#define INITIAL_FREE_PHYS align4k(AOS_KERNEL_ADDR + AOS_KERNEL_SIZE)
#define END_FREE_PHYS INITIAL_FREE_PHYS + 0x200000
static uint64_t phys_alloc_ptr;

static inline uint64_t avmf_alloc_phys_page(void) {
    if (phys_alloc_ptr + PAGE_SIZE > END_FREE_PHYS) {
        serial_print("[AVMF] Out of physical memory!\n");
        return 0;
    }
    uint64_t page = phys_alloc_ptr;
    phys_alloc_ptr += PAGE_SIZE;
    return page;
}

uint64_t avmf_virt_to_phys(uint64_t virt) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) {serial_print("[AVMF] Trying to get info on a unmapped region (physical addr)!\n"); return 0;} // Unmapped region
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
    serial_print("[AVMF] Trying to get info on a unmapped region (virtual addr)!\n");
    return NULL;
}

uint64_t avmf_alloc_phys_contiguous(uint64_t size) {
    uint64_t sz = align4k(size);
    if (phys_alloc_ptr + size > END_FREE_PHYS) {
        serial_print("[AVMF] Out of physical memory!\n");
        return 0;
    }
    uint64_t base = phys_alloc_ptr;
    phys_alloc_ptr += sz;
    return base;
}

void* avmf_map_phys_to_virt(uint64_t phys, uint64_t size, int flags) {
    void* virt = (void*)avmf_alloc_region(size, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);
    pager_map_range((uint64_t)virt, phys, size, flags);
    return virt;
}

void avmf_init(uint64_t base_virt, uint64_t region_size) {
    avmf_base = align4k(base_virt);
    avmf_limit = avmf_base + align4k(region_size);
    avmf_head = (avmf_header_t*)NULL;
    phys_alloc_ptr = (uint64_t)INITIAL_FREE_PHYS;
}

uint64_t avmf_alloc_region(uint64_t size, uint32_t flags) {
    size = align4k(size);
    uint64_t addr = avmf_base;
    avmf_header_t* last_node = NULL;

    // find the end of the current alloc list
    if (avmf_head) {
        avmf_header_t* current = avmf_head;
        while (current->next) {
            current = current->next;
        }
        last_node = current;
        addr = last_node->virt_addr + last_node->size;
    }

    // check bounds
    if (addr + size > avmf_limit) return 0;

    avmf_header_t* node = (avmf_header_t*)NULL;

    static avmf_header_t static_headers[128];
    static int header_index = 0;
    if (header_index >= 128) return 0;
    node = &static_headers[header_index++];
    
    node->virt_addr = addr;
    node->phys_addr = phys_alloc_ptr;
    phys_alloc_ptr += size;
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
        last_node->next = node;
    }

    return addr;
}

int avmf_map(uint64_t virt, uint64_t phys, uint32_t flags) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) return -1;

    region->phys_addr = phys;
    region->flags |= flags;
    return 0;
}

int avmf_map_identity_virt(uint64_t virt, uint64_t phys, uint32_t flags) { // Works for only Identity mapping and maps phys to virt without pager
    avmf_header_t* region = NULL;
    avmf_header_t* cur = avmf_head;
    while (cur) {
        if (phys >= cur->phys_addr && phys < cur->phys_addr + cur->size) {
            region = cur;
            break;
        }
        cur = cur->next;
    }
    if (!region) return -1;

    region->virt_addr = virt;
    region->flags |= flags;
    return 0;
}

avmf_header_t* avmf_find(uint64_t virt) {
    if (last_found && virt >= last_found->virt_addr && virt < last_found->virt_addr + last_found->size) {
        return last_found;
    }
    avmf_header_t* cur = avmf_head;
    while (cur) {
        if (virt >= cur->virt_addr && virt < cur->virt_addr + cur->size) {
            last_found = cur;
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
