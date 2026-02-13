#include <inttypes.h>
#include <system.h>
#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>
#include <inc/drivers/io/io.h>

#define E820_TYPE_RAM 1
#define E820_TYPE_RESERVED 2
#define E820_TYPE_ACPI_RECLAIM 3
#define E820_TYPE_ACPI_NVS 4
#define E820_TYPE_BAD 5

#define PAGE_HUGE (1 << 7)

struct bs1_e820_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t ext;
} __attribute__((packed));

struct bs1_e820 {
    uint32_t entry_count;
    struct bs1_e820_entry entries[];
} __attribute__((packed));

static struct page_table* kernel_pml4;
static struct page_table* mapped_pml4;
static uint8_t pager_ready = 0;

static inline void* pager_phys_to_virt(uint64_t phys) {
    if (!pager_ready) {
        return (void*)phys; // Identity
    }
    return (void*)(phys + AOS_DIRECT_MAP_BASE);
}

static struct page_table* alloc_page_table(uint64_t* phys_out) {
    uint64_t phys = avmf_alloc_phys_contiguous(PAGE_SIZE);
    uint64_t virt = avmf_alloc_virt(PAGE_SIZE, MALLOC_TYPE_SENSITIVE);

    if (!virt) {
        serial_print("[PAGER] No virtual address?\n");
        return NULL;
    };
    avmf_alloc_region(virt, phys, PAGE_SIZE, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);

    volatile struct page_table* tbl = NULL;
    tbl = (volatile struct page_table*)phys;
 
    for (int i = 0; i < 512; i++) {
        tbl->entries[i] = 0;
    }

    *phys_out = phys;
    
    return tbl;
}

static inline void load_cr3(uint64_t pml4_phys) {
    asm volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

void pager_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t remaining = size - offset;
        uint64_t cur_virt = virt + offset;
        uint64_t cur_phys = phys + offset;

        if (remaining >= 0x200000 && (cur_virt % 0x200000 == 0) && (cur_phys % 0x200000 == 0)) {
            pager_map(cur_virt, cur_phys, flags | PAGE_HUGE);
            offset += 0x200000;
        } else {
            pager_map(cur_virt, cur_phys, flags);
            offset += PAGE_SIZE;
        }
    }
}

void pager_init(void) {
    pager_ready = 0; // Ensure

    struct bs1_e820* e820 = (struct bs1_e820*)AOS_E820_INFO_ADDR;
    uint64_t max_phys_addr = 0;
    uint64_t base_phys[128];
    uint64_t limit_phys[128];
    uint64_t phys_idx = 0;
    for (int i = 0; i < e820->entry_count; i++) {
        struct bs1_e820_entry* e = &e820->entries[i];
        uint64_t end_addr = e->base + e->len;
        if (end_addr > max_phys_addr)
            max_phys_addr = end_addr;

        if (e->len == 0) continue;
        if (e->type == E820_TYPE_RAM) {
            serial_printf("E820: %p - %p (%llu MB) (Type RAM)\n", e->base, end_addr, e->len / 1024 / 1024);
            uint64_t start = e->base;
            // Don't use the first 2MB!
            if (start < 0x200000) {
                if (e->len <= (0x200000 - start)) continue; // Too small
                start = 0x200000;
            }
            base_phys[phys_idx] = start;
            limit_phys[phys_idx] = end_addr;
            phys_idx++;
        } else {
            serial_printf("E820: %p - %p (Type %d)\n", e->base, end_addr, e->type);
        }
    }

    avmf_init((uint64_t*)base_phys, (uint64_t*)limit_phys, phys_idx);
    serial_print("[PAGER] Initialized AVMF\n");

    uint64_t kernel_pml4_phys = 0;
    kernel_pml4 = alloc_page_table(&kernel_pml4_phys);
    serial_print("[PAGER] Allocated Virtual Memory for Page Tables\n");
    pager_map_range(AOS_DIRECT_MAP_BASE, 0x0, max_phys_addr, PAGE_PRESENT | PAGE_RW);
    serial_print("[PAGER] Mapped Direct Map\n");
    pager_map_range(0x0, 0x0, 0x1000000, PAGE_PRESENT | PAGE_RW); // Identity Map the Kernel (16 MB)
    serial_print("[PAGER] Mapped Kernel\n");

    pager_load(kernel_pml4);

    pager_ready = 1;
    serial_print("[PAGER] Everything is Set!\n");
}

struct page_table* pager_map(virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    struct page_table* pml4 = kernel_pml4;

    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd = (virt >> 21) & 0x1FF;
    int idx_pt = (virt >> 12) & 0x1FF;

    struct page_table* pdpt, *pd, *pt;

    if (!(pml4->entries[idx_pml4] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = 0;
        pdpt = alloc_page_table(&pdpt_phys);
        pml4->entries[idx_pml4] = pdpt_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pdpt = (struct page_table*)pager_phys_to_virt(pml4->entries[idx_pml4] & ~0xFFFULL);
    }
    if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) {
        uint64_t pd_phys = 0;
        pd = alloc_page_table(&pd_phys);
        pdpt->entries[idx_pdpt] = pd_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pd = (struct page_table*)pager_phys_to_virt(pdpt->entries[idx_pdpt] & ~0xFFFULL);
    }
    
    if (flags & PAGE_HUGE) {
        pd->entries[idx_pd] = (phys & ~0x1FFFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
    } else {
        if (!(pd->entries[idx_pd] & PAGE_PRESENT)) {
            uint64_t pt_phys = 0;
            pt = alloc_page_table(&pt_phys);
            pd->entries[idx_pd] = pt_phys | PAGE_PRESENT | PAGE_RW;
        } else {
            pt = (struct page_table*)pager_phys_to_virt(pd->entries[idx_pd] & ~0xFFFULL);
        }

        // Map the physical range
        pt->entries[idx_pt] = (phys & ~0xFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
    }
    return pml4;
}

void pager_destroy_table(int level) {
    struct page_table* table = mapped_pml4;
    if (!table) return;
    if (level > 1) {
        for (int i = 0; i < 512; i++) {
            if (table->entries[i] & PAGE_PRESENT) {
                if (level == 2 && (table->entries[i] & PAGE_HUGE)) continue;

                uint64_t phys = table->entries[i] & ~0xFFFULL;
                struct page_table* sub_table = (struct page_table*)pager_phys_to_virt(phys);
                pager_destroy_table(level - 1);
            }
        }
    }

    uint64_t phys_addr = avmf_virt_to_phys((uint64_t)table);
    avmf_free_phys((uint64_t)table);
}

static void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

void pager_unmap(uint64_t virt) {
    struct page_table* pml4 = mapped_pml4;
    if (!pml4) return;

    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd = (virt >> 21) & 0x1FF;
    int idx_pt = (virt >> 12) & 0x1FF;

    if (!(pml4->entries[idx_pml4] & PAGE_PRESENT)) return;
    struct page_table* pdpt = (struct page_table*)pager_phys_to_virt(pml4->entries[idx_pml4] & ~0xFFFULL);
    if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) return;
    struct page_table* pd = (struct page_table*)pager_phys_to_virt(pdpt->entries[idx_pdpt] & ~0xFFFULL);

    if (pd->entries[idx_pd] & PAGE_HUGE) {
        pd->entries[idx_pd] &= ~PAGE_PRESENT;
        invlpg(virt);
        return;
    }

    if (!(pd->entries[idx_pd] & PAGE_PRESENT)) return;
    struct page_table* pt = (struct page_table*)pager_phys_to_virt(pd->entries[idx_pd] & ~0xFFFULL);

    pt->entries[idx_pt] &= ~PAGE_PRESENT;
    invlpg(virt);
}

void pager_load(struct page_table* pml4) {
    uint64_t pml4_phys = (uint64_t)pml4;
    if (pager_ready) {
        pml4_phys = avmf_virt_to_phys((uint64_t)pml4);
        if (pml4_phys == 0)
            pml4_phys = (uint64_t)pml4;
    }
    
    load_cr3(pml4_phys);
    mapped_pml4 = pml4;
}
