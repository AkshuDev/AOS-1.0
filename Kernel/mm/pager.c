#include <inttypes.h>
#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>
#include <inc/io.h>

#define PAGE_HUGE (1 << 7)

static struct page_table* kernel_pml4;

static struct page_table* alloc_page_table(void) {
    uint64_t virt = avmf_alloc_region(PAGE_SIZE, AVMF_FLAG_PRESENT);
    uint64_t phys = avmf_virt_to_phys(virt);

    avmf_map_identity_virt(phys, phys, AVMF_FLAG_PRESENT);
    struct page_table* tbl = (struct page_table*)phys;

    if (!virt) {
        serial_print("[PAGER] No virtual address?\n");
        return NULL;
    };
    for (int i = 0; i < 512; i++) {
        tbl->entries[i] = 0;
    }
    
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

void pager_init(uint64_t fb_phys, uint64_t fb_size) {
    kernel_pml4 = alloc_page_table();

    pager_map_range(0x0, 0x0, 0x1600000, PAGE_PRESENT | PAGE_RW); // Identity Map the first 16MiB (2MB)
    if (fb_phys && fb_size) {
        pager_map_range(0xFFFFFFFF40000000, fb_phys, fb_size, PAGE_PRESENT | PAGE_RW);
    }

    pager_load(kernel_pml4);
}

struct page_table* pager_map(virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    struct page_table* pml4 = kernel_pml4;

    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd = (virt >> 21) & 0x1FF;
    int idx_pt = (virt >> 12) & 0x1FF;

    struct page_table* pdpt, *pd, *pt;

    if (!(pml4->entries[idx_pml4] & PAGE_PRESENT)) {
        pdpt = alloc_page_table();
        uint64_t pdpt_phys = avmf_virt_to_phys((uint64_t)pdpt);
        pml4->entries[idx_pml4] = pdpt_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pdpt = (struct page_table*)avmf_phys_to_virt(pml4->entries[idx_pml4] & ~0xFFFULL);
    }
    if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) {
        pd = alloc_page_table();
        uint64_t pd_phys = avmf_virt_to_phys((uint64_t)pd);
        pdpt->entries[idx_pdpt] = pd_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pd = (struct page_table*)avmf_phys_to_virt(pdpt->entries[idx_pdpt] & ~0xFFFULL);
    }
    
    if (flags & PAGE_HUGE) {
        pd->entries[idx_pd] = (phys & ~0x1FFFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
    } else {
        if (!(pd->entries[idx_pd] & PAGE_PRESENT)) {
            pt = alloc_page_table();
            uint64_t pt_phys = avmf_virt_to_phys((uint64_t)pt);
            pd->entries[idx_pd] = pt_phys | PAGE_PRESENT | PAGE_RW;
        } else {
            pt = (struct page_table*)avmf_phys_to_virt(pd->entries[idx_pd] & ~0xFFFULL);
        }

        // Map the physical range
        pt->entries[idx_pt] = (phys & ~0xFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
    }
    return pml4;
}

void pager_load(struct page_table* pml4) {
    uint64_t pml4_phys = avmf_virt_to_phys((uint64_t)pml4);
    load_cr3(pml4_phys);
}
