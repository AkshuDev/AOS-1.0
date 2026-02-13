#include <inttypes.h>
#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/drivers/io/io.h>

#include <stddef.h>

#define PAGE_SIZE 0x1000
#define AVMF_STATIC_SIZE 2048
#define BITMAP_SIZE 131072

static avmf_header_t* avmf_head;
static avmf_header_t* last_found;

static spinlock_t avmf_lock = 0;
static spinlock_t avmf_lock2 = 0;

static avmf_header_t static_headers[AVMF_STATIC_SIZE];
static int header_index = 0;

static uint64_t avmf_limit[128];
static uint64_t avmf_base[128];
static uint64_t avmf_bitmap[BITMAP_SIZE];

static uint64_t heap_kernel = AOS_KERNEL_SPACE_BASE;
static uint64_t heap_driver = AOS_DRIVER_SPACE_BASE;
static uint64_t heap_user = AOS_USER_SPACE_BASE;
static uint64_t heap_sensitive = AOS_SENSITIVE_SPACE_BASE;

static inline uint64_t align4k(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static void bitmap_set(uint64_t page_idx) {
    avmf_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
}

static void bitmap_clear(uint64_t page_idx) {
    avmf_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
}

static uint8_t bitmap_test(uint64_t page_idx) {
    return avmf_bitmap[page_idx / 8] & (1 << (page_idx % 8));
}

static uint64_t avmf_alloc_phys_page(void) {
    for (int i = 0; i < 128; i++) {
        if (avmf_limit[i] == 0) continue;

        uint64_t start_page = avmf_base[i] / PAGE_SIZE;
        uint64_t end_page = avmf_limit[i] / PAGE_SIZE;

        for (uint64_t p = start_page; p < end_page; p++) {
            if (!bitmap_test(p)) {
                bitmap_set(p);
                return p * PAGE_SIZE;
            }
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
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    uint64_t sz = align4k(size);
    uint64_t pages_needed = sz / PAGE_SIZE;

    for (int i = 0; i < 128; i++) {
        if (avmf_limit[i] == 0) continue;

        uint64_t start_page = avmf_base[i] / PAGE_SIZE;
        uint64_t end_page = avmf_limit[i] / PAGE_SIZE;
        uint64_t consecutive = 0;
        uint64_t first_page = 0;

        for (uint64_t p = start_page; p < end_page; p++) {
            if (!bitmap_test(p)) {
                if (consecutive == 0) first_page = p;
                consecutive++;

                if (consecutive == pages_needed) {
                    for (uint64_t j = first_page; j < first_page + pages_needed; j++) {
                        bitmap_set(j);
                    }
                    spin_unlock_irqrestore(&avmf_lock, rflags);
                    return first_page * PAGE_SIZE;
                }
            } else {
                consecutive = 0;
            }
        }
    }

    serial_printf("[AVMF] Out of physical memory (Range: 0x%llx bytes)\n", sz);
    spin_unlock_irqrestore(&avmf_lock, rflags);
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
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    *heap_ptr += true_size;
    spin_unlock_irqrestore(&avmf_lock, rflags);

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
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    *heap_ptr += true_size;
    if (phys_out != NULL) *phys_out = phys;
    spin_unlock_irqrestore(&avmf_lock, rflags);
    return (uint64_t)(*heap_ptr - true_size);
}

void avmf_free(uint64_t virt) {
    avmf_header_t* hdr = avmf_find(virt);
    if (!hdr) return;
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);

    uint64_t phys = hdr->phys_addr;
    uint64_t page_idx = phys / PAGE_SIZE;
    uint64_t pages = hdr->size / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++) {
        bitmap_clear(page_idx + i);
    }
    pager_unmap(virt);
    spin_unlock_irqrestore(&avmf_lock, rflags);
}

void avmf_free_phys(uint64_t virt) {
    avmf_header_t* hdr = avmf_find(virt);
    if (!hdr) return;
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);

    uint64_t phys = hdr->phys_addr;
    uint64_t page_idx = phys / PAGE_SIZE;
    uint64_t pages = hdr->size / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++) {
        bitmap_clear(page_idx + i);
    }
    spin_unlock_irqrestore(&avmf_lock, rflags);
}

void avmf_init(uint64_t* base_phys, uint64_t* limit_phys, uint8_t entries) {
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    uint8_t ent = entries <= 128 ? entries : 128;
    for (uint8_t i = 0; i < ent; i++) {
        avmf_base[i] = base_phys[i];
        avmf_limit[i] = limit_phys[i];
    }
    avmf_head = (avmf_header_t*)NULL;
    spin_unlock_irqrestore(&avmf_lock, rflags);
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
 
    if (header_index >= AVMF_STATIC_SIZE) return 0;
    uint64_t rflags = spin_lock_irqsave(&avmf_lock2);
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
    spin_unlock_irqrestore(&avmf_lock2, rflags);

    return 1;
}

int avmf_map(uint64_t virt, uint64_t phys, uint32_t flags) {
    avmf_header_t* region = avmf_find(virt);
    if (!region) {serial_print("[AVMF] Did not find region for mapping!\n"); return 0;}

    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    region->phys_addr = phys;
    region->flags |= flags;
    spin_unlock_irqrestore(&avmf_lock, rflags);
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

    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    region->virt_addr = virt;
    region->flags |= flags;
    spin_unlock_irqrestore(&avmf_lock, rflags);
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
