#include <aos_inttypes.h>
#include <system.h>
#include <e820.h>

#include <inc/core/kfuncs.h>
#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>
#include <inc/drivers/io/io.h>

#define PAGE_HUGE (1 << 7)

static struct page_table* kernel_pml4 = NULL;
static struct page_table* mapped_pml4 = NULL;
static aos_bool pager_ready = AOS_FALSE;
static uint64_t cpu_phys_bits = 0;
static uint64_t cpu_virt_bits = 0;

__attribute__((aligned(PAGE_SIZE)))
static uint64_t pre_pml4[512] = {0};
__attribute__((aligned(PAGE_SIZE)))
static uint64_t pre_pdpt[512] = {0};
__attribute__((aligned(PAGE_SIZE)))
static uint64_t pre_pd[512] = {0};
__attribute__((aligned(PAGE_SIZE)))
static uint64_t pre_dm_pdpt[512] = {0};
__attribute__((aligned(PAGE_SIZE)))
static uint64_t pre_dm_pd[512] = {0};

static void* pager_phys_to_virt(uint64_t phys) {
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
    
    if (pager_ready != 1)
        tbl = (volatile struct page_table*)phys;
    else {
        tbl = (volatile struct page_table*)(AOS_DIRECT_MAP_BASE + phys);
    }
    if (!tbl) { serial_print("[PAGER] Failed to allocate page table\n"); return NULL; }
 
    for (int i = 0; i < 512; i++) {
        tbl->entries[i] = 0;
    }

    *phys_out = phys;

	memset(tbl, 0, PAGE_SIZE);
    
    return tbl;
}

static void load_cr3(uint64_t pml4_phys) {
    asm volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

static uint64_t bits_needed(uint64_t n) {
    uint64_t count = 0;
    do {
        count++;
        n >>= 1;
    } while (n);
    return count;
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
    pager_ready = AOS_FALSE; // Ensure

    struct bs1_e820* e820 = (struct bs1_e820*)AOS_E820_INFO_ADDR;
    uint64_t max_phys_addr = 0;
    uint64_t base_phys[128];
    uint64_t limit_phys[128];
    uint64_t phys_idx = 0;
	extern uint8_t __bss_end; // from linker script
	uintptr_t bss_end = (uintptr_t)((uintptr_t)AOS_KERNEL_ADDR + (uintptr_t)&__bss_end);
    for (int i = 0; i < e820->entry_count; i++) {
        struct bs1_e820_entry* e = &e820->entries[i];
        uint64_t end_addr = e->base + e->len;
        
        if (e->len == 0) continue;
		if (end_addr > max_phys_addr)
            max_phys_addr = end_addr;

		serial_printf("E820: %p - %p (%llu MB) (Type %s [%d])\n", e->base, end_addr, (uint64_t)((float)e->len / 1024.0f / 1024.0f), e820_get_type_str(e->type), e->type);
        if (e->type == E820_TYPE_RAM) {
            uint64_t start = e->base;
            // Don't use the first <Kernel End>MB!
            if (start < bss_end) {
                if (e->len <= (bss_end - start)) continue; // Too small
                start = bss_end;
            }
            base_phys[phys_idx] = start;
            limit_phys[phys_idx] = end_addr;
            phys_idx++;
        }
    }

    // Get max bits
    unsigned int eax, ebx, ecx, edx;
    eax = 0x80000008;
    ecx = 0;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    cpu_phys_bits = eax & 0xFF;
    cpu_virt_bits = (eax >> 8) & 0xFF;
	uint64_t max_addr = (1ULL << cpu_phys_bits) - 1;

    uint64_t bits_req = bits_needed(max_phys_addr);

    if (bits_req > cpu_phys_bits) {
        if (cpu_phys_bits >= 64)
            max_phys_addr = ~0ULL;
        else
            max_phys_addr = (1ULL << cpu_phys_bits) - 1;
        serial_printf("[PAGER] CPU Doesn't Support required %lu bits, hence maximum memory used will be %lu bits or %lu GB\n", bits_req, cpu_phys_bits, max_phys_addr / (1024 * 1024 * 1024));
    }

    avmf_init((uint64_t*)base_phys, (uint64_t*)limit_phys, phys_idx);
    serial_print("[PAGER] Initialized AVMF\n");

    uint64_t kernel_pml4_phys = 0;
    while (kernel_pml4 == NULL) {
        kernel_pml4 = alloc_page_table(&kernel_pml4_phys);
        if (!kernel_pml4) {
            serial_print("[PAGER] Failed to allocate kernel PML4, retrying...\n");
        }
    }
    serial_print("[PAGER] Allocated Virtual Memory for Page Tables\n");

    serial_printf("[PAGER] Mapping Direct Map (0x%lx-0x%lx) to 0x%lx\n", 0x0, max_phys_addr, AOS_DIRECT_MAP_BASE);
    pager_map_range(AOS_DIRECT_MAP_BASE, 0x0, max_phys_addr, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    
	serial_print("[PAGER] Mapped Direct Map\n");
    pager_map_range(0x0, 0x0, bss_end, PAGE_PRESENT | PAGE_RW); // Identity Map the Kernel (<Kernel End> MB)
    serial_printf("[PAGER] Mapped Kernel (0x0-0x%x)\n", bss_end);

    pager_load(kernel_pml4);

    pager_ready = AOS_TRUE;
    serial_print("[PAGER] Everything is Set!\n");
}

static void invlpg(uint64_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

struct page_table* pager_map(virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    if (phys & ~((1ULL << cpu_phys_bits) - 1)) {
        serial_printf("[PAGER] ERROR: Physical address %p exceeds CPU limit of %lu bits!\n", phys, cpu_phys_bits);
        return NULL;
    }

    struct page_table* pml4 = kernel_pml4;

    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd = (virt >> 21) & 0x1FF;
    int idx_pt = (virt >> 12) & 0x1FF;

    struct page_table* pdpt, *pd, *pt = NULL;

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
	invlpg(virt);
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
