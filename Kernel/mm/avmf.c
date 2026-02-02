#include <inttypes.h>
#include <system.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/io.h>

#include <stddef.h>

#define PAGE_SIZE 0x1000
#define AVMF_STATIC_SIZE 2048

static avmf_header_t* avmf_head;
static avmf_header_t* last_found;

static uint64_t avmf_limit[128];
static uint64_t avmf_base[128];
static uint64_t avmf_cur_idx_ptr = 0;

static uint64_t heap_kernel = AOS_KERNEL_SPACE_BASE;
static uint64_t heap_driver = AOS_DRIVER_SPACE_BASE;
static uint64_t heap_user = AOS_USER_SPACE_BASE;
static uint64_t heap_sensitive = AOS_SENSITIVE_SPACE_BASE;

static inline uint64_t align4k(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static uint64_t phys_alloc_ptr;

static uint64_t avmf_alloc_phys_page(void) {
    if (avmf_cur_idx_ptr >= 128) {serial_print("[AVMF] Out of Physical Memory (IDX Cause)\n"); return 0;}

    while (avmf_cur_idx_ptr < 128) {
        if (phys_alloc_ptr < avmf_base[avmf_cur_idx_ptr]) {
            phys_alloc_ptr = avmf_base[avmf_cur_idx_ptr];
        }

        if ((phys_alloc_ptr + PAGE_SIZE) <= avmf_limit[avmf_cur_idx_ptr]) {
            uint64_t alloc_phys = phys_alloc_ptr;
            phys_alloc_ptr += PAGE_SIZE;
            return alloc_phys;
        }

        avmf_cur_idx_ptr++;
        if (avmf_cur_idx_ptr < 128 && avmf_limit[avmf_cur_idx_ptr] != 0) {
            phys_alloc_ptr = avmf_base[avmf_cur_idx_ptr];
        } else {
            break;
        }
    }

    serial_print("[AVMF] Out of physical memory!\n");
    return 0;
}

uint64_t avmf_virt_to_phys(uint64_t virt) {
    if (virt >= AOS_DIRECT_MAP_BASE && virt < AOS_KERNEL_SPACE_BASE)
        return (uint64_t)(virt - AOS_DIRECT_MAP_BASE); // Present in Direct Map

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

    if (avmf_cur_idx_ptr >= 128) {serial_printf("[AVMF] Out of Physical Memory (IDX Cause, Range: 0x%llx bytes)\n", sz); return 0;}

    while (avmf_cur_idx_ptr < 128) {
        if (phys_alloc_ptr < avmf_base[avmf_cur_idx_ptr]) {
            phys_alloc_ptr = avmf_base[avmf_cur_idx_ptr];
        }

        if ((phys_alloc_ptr + sz) <= avmf_limit[avmf_cur_idx_ptr]) {
            uint64_t alloc_phys = phys_alloc_ptr;
            phys_alloc_ptr += sz;
            return alloc_phys;
        }

        avmf_cur_idx_ptr++;
        if (avmf_cur_idx_ptr < 128 && avmf_limit[avmf_cur_idx_ptr] != 0) {
            phys_alloc_ptr = avmf_base[avmf_cur_idx_ptr];
        } else {
            break;
        }
    }

    serial_printf("[AVMF] Out of physical memory (Range: 0x%llx bytes)\n", sz);
    return 0;
}

uint64_t avmf_alloc_virt(uint64_t size, MemoryAllocType type) {
    uint64_t* heap_ptr = NULL;
    uint64_t heap_end = 0;
    switch (type) {
        case MALLOC_TYPE_USER:
            heap_ptr = &heap_user;
            heap_end = AOS_DIRECT_MAP_BASE;
            break;
        case MALLOC_TYPE_KERNEL:
            heap_ptr = &heap_kernel;
            heap_end = AOS_DRIVER_SPACE_BASE;
            break;
        case MALLOC_TYPE_DRIVER:
            heap_ptr = &heap_driver;
            heap_end = AOS_SENSITIVE_SPACE_BASE;
            break;
        case MALLOC_TYPE_SENSITIVE:
            heap_ptr = &heap_sensitive;
            heap_end = (uint64_t)-1;
            break;
        default: 
            serial_print("[AVMF] Invalid VMemory Allocation Type!\n");
            return 0;
    }
    uint64_t true_size = align4k(size);

    if (!heap_ptr) {serial_print("[AVMF] Internal Error During VMemory Allocation!\n"); return 0;}
    if (*heap_ptr + true_size > heap_end) {serial_print("[AVMF] Not Enough VMemory for Allocation!\n"); return 0;}
    
    uint64_t ptr = *heap_ptr;
    *heap_ptr += true_size;

    return ptr;
}

uint64_t avmf_alloc(uint64_t size, MemoryAllocType type, int flags, uint64_t* phys_out) {
    uint64_t* heap_ptr = NULL;
    uint64_t heap_end = 0;
    switch (type) {
        case MALLOC_TYPE_USER:
            heap_ptr = &heap_user;
            heap_end = AOS_DIRECT_MAP_BASE;
            break;
        case MALLOC_TYPE_KERNEL:
            heap_ptr = &heap_kernel;
            heap_end = AOS_DRIVER_SPACE_BASE;
            break;
        case MALLOC_TYPE_DRIVER:
            heap_ptr = &heap_driver;
            heap_end = AOS_SENSITIVE_SPACE_BASE;
            break;
        case MALLOC_TYPE_SENSITIVE:
            heap_ptr = &heap_sensitive;
            heap_end = 0;
            break;
        default:
            serial_print("[AVMF] Invalid VMemory Allocation Type!\n");
            return 0;
    }
    uint64_t true_size = align4k(size);

    if (!heap_ptr) {serial_print("[AVMF] Internal Error During VMemory Allocation!\n"); return 0;}
    if (*heap_ptr + true_size > heap_end) {serial_print("[AVMF] Not Enough VMemory for Allocation!\n"); return 0;}

    uint64_t phys = avmf_alloc_phys_contiguous(true_size);
    if (!phys) {serial_print("[AVMF] Unable to retrieve physical address of VMemory Allocation!\n"); return 0;}

    if (!avmf_alloc_region((uint64_t)*heap_ptr, phys, true_size, flags)) {serial_print("[AVMF] Failed to Allocate Internal Region for VMemory!\n"); return 0;}
    pager_map_range((uint64_t)*heap_ptr, phys, true_size, flags);
    *heap_ptr += true_size;
    *phys_out = phys;
    return (uint64_t)(*heap_ptr - true_size);
}

void avmf_init(uint64_t* base_phys, uint64_t* limit_phys, uint8_t entries) {
    uint8_t ent = entries <= 128 ? entries : 128;
    for (uint8_t i = 0; i < ent; i++) {
        avmf_base[i] = base_phys[i];
        avmf_limit[i] = limit_phys[i];
    }
    avmf_head = (avmf_header_t*)NULL;
    phys_alloc_ptr = (uint64_t)avmf_base[0];
}

uint8_t avmf_alloc_region(uint64_t virt, uint64_t phys, uint64_t size, uint32_t flags) {
    size = align4k(size);
    avmf_header_t* last_node = NULL;

    // find the end of the current alloc list
    if (avmf_head) {
        avmf_header_t* current = avmf_head;
        while (current->next) {
            current = current->next;
        }
        last_node = current;
    }

    avmf_header_t* node = (avmf_header_t*)NULL;

    static avmf_header_t static_headers[AVMF_STATIC_SIZE];
    static int header_index = 0;
    if (header_index >= AVMF_STATIC_SIZE) return 0;
    node = &static_headers[header_index++];
    
    node->virt_addr = virt;
    node->phys_addr = phys;
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

    return 1;
}

int avmf_map(uint64_t virt, uint64_t phys, uint32_t flags) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) {serial_print("[AVMF] Did not find region for mapping!\n"); return 0;}

    region->phys_addr = phys;
    region->flags |= flags;
    return 1;
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
    if (!region) {serial_print("[AVMF] Did not find region for identity mapping!\n"); return 0;}

    region->virt_addr = virt;
    region->flags |= flags;
    return 1;
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
        serial_printf("[AVMF] VIRT=%llx PHYS=%llx SIZE=%llu FLAGS=%x\n", cur->virt_addr, cur->phys_addr, cur->size, cur->flags);
        cur = cur->next;
    }
}
