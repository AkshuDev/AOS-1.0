#include <inttypes.h>
#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>

static struct page_table* kernel_pml4;

static struct page_table* alloc_page_table(void) {
    struct page_table* tbl = (struct page_table*)avmf_alloc_region(PAGE_SIZE, AVMF_FLAG_PRESENT);
    if (!tbl) return NULL;
    for (int i = 0; i < 512; i++) {
        tbl->entries[i] = 0;
    }
    return tbl;
}

static inline void load_cr3(uint64_t pml4_phys) {
    asm volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

void pager_init(uint64_t fb_phys, uint64_t fb_size) {
    kernel_pml4 = alloc_page_table();

    for (uint64_t addr = 0; addr < 0x200000; addr += PAGE_SIZE) {
        pager_map(addr, addr, PAGE_PRESENT | PAGE_RW);
    }

    if (fb_phys != 0 && fb_size != 0) {
        for (uint64_t i = 0; i < fb_size; i += PAGE_SIZE) {
            pager_map(0xFFFF800000000000 + i, fb_phys + i, PAGE_PRESENT | PAGE_RW);
        }
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
        pml4->entries[idx_pml4] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;
    } else {
        pdpt = (struct page_table*)(pml4->entries[idx_pml4] & ~0xFFFULL);
    }
    if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) {
        pd = alloc_page_table();
        pdpt->entries[idx_pdpt] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;
    } else {
        pd = (struct page_table*)(pdpt->entries[idx_pdpt] & ~0xFFFULL);
    }
    if (!(pd->entries[idx_pd] & PAGE_PRESENT)) {
        pt = alloc_page_table();
        pd->entries[idx_pd] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW;
    } else {
        pt = (struct page_table*)(pd->entries[idx_pd] & ~0xFFFULL);
    }

    // Map the physical range
    pt->entries[idx_pt] = (phys & ~0xFFFULL) | (flags & 0xFFFULL);
    return pml4;
}

void pager_load(struct page_table* pml4) {
    load_cr3((uint64_t)pml4);
}
