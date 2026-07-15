#include <aos_inttypes.h>
#include <system.h>
#include <uniboot.h>

#include <inc/core/kfuncs.h>
#include <inc/core/smp.h>
#include <inc/mm/pager.h>
#include <inc/mm/avmf.h>
#include <inc/drivers/io/io.h>

static struct page_table* kernel_pml4 = NULL;
static struct page_table* mapped_pml4 = NULL;
static aos_bool pager_ready = AOS_FALSE;
static uint64_t cpu_phys_bits = 0;
static uint64_t cpu_virt_bits = 0;

static uint64_t first_pagemaps_ptr = 0;

extern uint8_t __kstart; // from linker script
static uintptr_t kstart = (uintptr_t)&__kstart;

extern uint8_t __bss_end; // from linker script
static uintptr_t bss_end;

static uintptr_t first_pagemaps;
static uintptr_t first_pagemaps_end;

static inline const char* uniboot_smmap_get_type_str(uniboot_smmap_type type) {
	switch (type) {
		case UNIBOOT_SMMAP_TYPE_UNUSABLE: return "Unusable";
		case UNIBOOT_SMMAP_TYPE_RESERVED: return "Reserved";
		case UNIBOOT_SMMAP_TYPE_ACPI_NVS: return "ACPI NVS";
		case UNIBOOT_SMMAP_TYPE_ACPI_RECLAIM: return "ACPI Reclaim";
		case UNIBOOT_SMMAP_TYPE_FREE: return "Free";
		default: return "Unknown";
	}
}

static void pager_dump_mapping(struct page_table *pml4, uint64_t virt) {
    int pml4_i = (virt >> 39) & 0x1FF;
    int pdpt_i = (virt >> 30) & 0x1FF;
    int pd_i = (virt >> 21) & 0x1FF;
    int pt_i = (virt >> 12) & 0x1FF;

    serial_printf("=== VA 0x%x ===\n", virt);

    serial_printf("PML4[%d] = 0x%x\n", pml4_i, pml4->entries[pml4_i]);
    if (!(pml4->entries[pml4_i] & PAGE_PRESENT)) return;
    struct page_table* pdpt = (struct page_table*)(pml4->entries[pml4_i] & ~0xFFFULL);

    serial_printf("PDPT[%d] = 0x%x\n", pdpt_i, pdpt->entries[pdpt_i]);
    if (!(pdpt->entries[pdpt_i] & PAGE_PRESENT)) return;
    struct page_table* pd = (struct page_table*)(pdpt->entries[pdpt_i] & ~0xFFFULL);

    serial_printf("PD[%d] = 0x%x\n", pd_i, pd->entries[pd_i]);
    if (pd->entries[pd_i] & PAGE_HUGE) {
        serial_print("2 MiB page\n");
        return;
    }

    if (!(pd->entries[pd_i] & PAGE_PRESENT)) return;
    struct page_table* pt = (struct page_table*)(pd->entries[pd_i] & ~0xFFFULL);

    serial_printf("PT[%d] = 0x%x\n", pt_i, pt->entries[pt_i]);
}

static void* pager_phys_to_virt(uint64_t phys) {
    if (!pager_ready) {
        return (void*)phys; // Identity
    }
    return (void*)(phys + AOS_DIRECT_MAP_BASE);
}

static struct page_table* alloc_page_table(uint64_t* phys_out) {
	volatile struct page_table* tbl = NULL;
	uint64_t phys = 0;
	uint64_t virt = 0;

	if (pager_ready) {
		phys = avmf_alloc_phys_contiguous(sizeof(struct page_table));
		virt = avmf_alloc_virt(sizeof(struct page_table), MALLOC_TYPE_SENSITIVE);

		if (!virt) {
			serial_print("[PAGER] No virtual address?\n");
			return NULL;
		};
		avmf_alloc_region(virt, phys, sizeof(struct page_table), AVMF_FLAG_WRITEABLE);
		tbl = (volatile struct page_table*)(AOS_DIRECT_MAP_BASE + phys);
	} else {
		phys = first_pagemaps + first_pagemaps_ptr;
		if (phys > first_pagemaps_end) {
			serial_print("[PAGER] No more space for pagemaps within kernel bss! Need full init of pager to continue\n");
			return NULL;
		}
		virt = phys; // Kernel is identity mapped

		first_pagemaps_ptr += sizeof(struct page_table);
		tbl = (volatile struct page_table*)virt;
	}
    if (!tbl) { serial_print("[PAGER] Failed to allocate page table\n"); return NULL; }
 
	memset(tbl, 0, sizeof(volatile struct page_table));
	*phys_out = phys;

    return tbl;
}

static void load_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
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
	bss_end = (uintptr_t)((uintptr_t)kstart + (uintptr_t)&__bss_end);

	uniboot_boot_info* binfo = kget_sysinfo();
    uniboot_smmap* m = kget_sysmap();
	if (!m || !binfo) {
		if (!m) serial_print("[PAGER] No System Memory Map Found!\n");
		if (!binfo) serial_print("[PAGER] No Boot Info Found!\n");
		serial_print("[PAGER] Cannot Proceed, hanging!\n");
		for (;;) __asm__ volatile("hlt"); // Cannot proceed
	}
    
	first_pagemaps = (uintptr_t)binfo->kernel_space;
	first_pagemaps_end = (uintptr_t)binfo->kernel_space_end;
	
	uint64_t max_phys_addr = 0;
    uint64_t base_phys[256];
    uint64_t limit_phys[256];
    uint64_t phys_idx = 0;
	
	for (int i = 0; i < m->count; i++) {
        uniboot_smmap_entry* e = &m->entries[i];
        uint64_t end_addr = e->phys_start + e->size;
        
        if (e->size == 0) continue;
		if (end_addr > max_phys_addr)
            max_phys_addr = end_addr;

		serial_printf("SMMAP: %p - %p (%llu MB) (Type %s [%d])\n", e->phys_start, end_addr, (uint64_t)((float)e->size / 1024.0f / 1024.0f), uniboot_smmap_get_type_str(e->type), e->type);
        if (e->type == UNIBOOT_SMMAP_TYPE_FREE) {
            uint64_t start = e->phys_start;
            // Don't use the first <Kernel End>MB!
            if (start < bss_end) {
                if (e->size <= (bss_end - start)) continue; // Too small
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
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
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
	if (!pager_ready) return;
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
	smp_tlb(addr, AOS_FALSE);
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

	uint64_t pml4_rflags = spin_lock_irqsave(&pml4->lock);
    if (!(pml4->entries[idx_pml4] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = 0;
        pdpt = alloc_page_table(&pdpt_phys);
        pml4->entries[idx_pml4] = pdpt_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pdpt = (struct page_table*)pager_phys_to_virt(pml4->entries[idx_pml4] & ~0xFFFULL);
    }
	
	uint64_t pdpt_rflags = spin_lock_irqsave(&pdpt->lock);
	spin_unlock_irqrestore(&pml4->lock, pml4_rflags);

    if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) {
        uint64_t pd_phys = 0;
        pd = alloc_page_table(&pd_phys);
        pdpt->entries[idx_pdpt] = pd_phys | PAGE_PRESENT | PAGE_RW;
    } else {
        pd = (struct page_table*)pager_phys_to_virt(pdpt->entries[idx_pdpt] & ~0xFFFULL);
    }
	
	uint64_t pd_rflags = spin_lock_irqsave(&pd->lock);
	spin_unlock_irqrestore(&pdpt->lock, pdpt_rflags);

    if (flags & PAGE_HUGE) {
		page_entry_t old = pd->entries[idx_pd];
		if (old & PAGE_PRESENT) {
			spin_unlock_irqrestore(&pd->lock, pd_rflags);
			if (old & PAGE_HUGE) {
				serial_printf(
					"[PAGER] HUGE already mapped Virt=%p Phys(old)=%p Phys(new)=%p Flags=%llx\n",
					virt,
					old & ~0x1FFFFFULL,
					phys & ~0x1FFFFFULL,
					old
				);
				if ((old & ~0x1FFFFFULL) == (phys & ~0x1FFFFFULL)) return pml4;
				return NULL;
			}
			serial_print("[PAGER] ERROR: PD entry already points to a PT!\n");
			return NULL;
		}
        
		pd->entries[idx_pd] = (phys & ~0x1FFFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
		spin_unlock_irqrestore(&pd->lock, pd_rflags);
    } else {
		if (pd->entries[idx_pd] & PAGE_HUGE) {
			serial_printf(
				"[PAGER] HUGE already mapped Virt=%p Phys(old)=%p Phys(new)=%p Flags=%llx\n",
				virt,
				pd->entries[idx_pd] & ~0x1FFFFFULL,
				phys & ~0x1FFFFFULL,
				pd->entries[idx_pd]
			);

			spin_unlock_irqrestore(&pd->lock, pd_rflags);

			uint64_t old_phys = pd->entries[idx_pd] & ~0x1FFFFFULL;
			if (old_phys == (phys & ~0x1FFFFFULL)) {
				return pml4;
			}
			serial_print("[PAGER] ERROR: PD entry already points to a PT!\n");
			return NULL;
		}
        if (!(pd->entries[idx_pd] & PAGE_PRESENT)) {
            uint64_t pt_phys = 0;
            pt = alloc_page_table(&pt_phys);
            pd->entries[idx_pd] = pt_phys | PAGE_PRESENT | PAGE_RW;
        } else {
            pt = (struct page_table*)pager_phys_to_virt(pd->entries[idx_pd] & ~0xFFFULL);
        }
		
		uint64_t pt_rflags = spin_lock_irqsave(&pt->lock);
		spin_unlock_irqrestore(&pd->lock, pd_rflags);
        // Map the physical range
		uint64_t old = pt->entries[idx_pt];
		if (old & PAGE_PRESENT) {
			serial_printf(
				"[PAGER] Page already mapped Virt=%p Phys(old)=%p Phys(new)=%p Flags=%llx\n",
				virt,
				old & ~0x1FFFFFULL,
				phys & ~0x1FFFFFULL,
				old
			);

			uint64_t old_phys = old & ~0xFFFULL;
			spin_unlock_irqrestore(&pt->lock, pt_rflags);

			if (old_phys == (phys & ~0xFFFULL)) {
				return pml4;
			}
    		serial_print("[PAGER] ERROR: Mapping already exists!\n");
			return NULL;
		}
		pt->entries[idx_pt] = (phys & ~0xFFFULL) | (flags & 0xFFFULL) | PAGE_PRESENT;
		spin_unlock_irqrestore(&pt->lock, pt_rflags);
    }

    return pml4;
}

static void destroy_table(struct page_table* table, int level, uint64_t lock_rflags, aos_bool locked) {
    if (!table) return;

	uint64_t rflags = locked ? lock_rflags : spin_lock_irqsave(&table->lock);
    if (level > 1) {
        for (int i = 0; i < 512; i++) {
            if (table->entries[i] & PAGE_PRESENT) {
                if (level == 2 && (table->entries[i] & PAGE_HUGE)) continue;
                
                uint64_t phys = table->entries[i] & ~0xFFFULL;
                struct page_table* sub_table = (struct page_table*)pager_phys_to_virt(phys);

                destroy_table(sub_table, level - 1, 0, AOS_FALSE);
				table->entries[i] = 0; 
            }
        }
    }
	spin_unlock_irqrestore(&table->lock, rflags);
    avmf_free_phys(avmf_virt_to_phys((uint64_t)table));
}

void pager_destroy_table(int level) {
    struct page_table* table = mapped_pml4;
    return destroy_table(table, level, 0, AOS_FALSE);
}

void pager_unmap(uint64_t virt) {
    struct page_table* pml4 = mapped_pml4;
    if (!pml4) return;

    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd = (virt >> 21) & 0x1FF;
    int idx_pt = (virt >> 12) & 0x1FF;

	uint64_t pml4_rflags = spin_lock_irqsave(&pml4->lock);
    
	if (!(pml4->entries[idx_pml4] & PAGE_PRESENT)) {
		spin_unlock_irqrestore(&pml4->lock, pml4_rflags);
		return;
	}
    struct page_table* pdpt = (struct page_table*)pager_phys_to_virt(pml4->entries[idx_pml4] & ~0xFFFULL);
	
	uint64_t pdpt_rflags = spin_lock_irqsave(&pdpt->lock);
	spin_unlock_irqrestore(&pml4->lock, pml4_rflags);
    
	if (!(pdpt->entries[idx_pdpt] & PAGE_PRESENT)) {
		spin_unlock_irqrestore(&pdpt->lock, pdpt_rflags);
		return;
	}
    struct page_table* pd = (struct page_table*)pager_phys_to_virt(pdpt->entries[idx_pdpt] & ~0xFFFULL);
	
	uint64_t pd_rflags = spin_lock_irqsave(&pd->lock);
    spin_unlock_irqrestore(&pdpt->lock, pdpt_rflags);
	
	if (pd->entries[idx_pd] & PAGE_HUGE) {
        pd->entries[idx_pd] &= ~PAGE_PRESENT;

		spin_unlock_irqrestore(&pd->lock, pd_rflags);

        invlpg(virt);
        return;
    }

    if (!(pd->entries[idx_pd] & PAGE_PRESENT)) {
		spin_unlock_irqrestore(&pd->lock, pd_rflags);
		return;
	}
    struct page_table* pt = (struct page_table*)pager_phys_to_virt(pd->entries[idx_pd] & ~0xFFFULL);

	uint64_t pt_rflags = spin_lock_irqsave(&pt->lock);
	spin_unlock_irqrestore(&pd->lock, pd_rflags);

    pt->entries[idx_pt] &= ~PAGE_PRESENT;

	spin_unlock_irqrestore(&pt->lock, pt_rflags);

    invlpg(virt);
}

void pager_load(struct page_table* pml4) {
    uint64_t pml4_phys = (uint64_t)pml4;

	uint64_t rflags = spin_lock_irqsave(&pml4->lock);

	serial_printf("[PAGER] Loading PML4 at Phys - 0x%llx\n", pml4_phys);
    
	if (pager_ready) {
        pml4_phys = avmf_virt_to_phys((uint64_t)pml4);
        if (pml4_phys == 0)
            pml4_phys = (uint64_t)pml4;
    }
    
    load_cr3(pml4_phys);

	spin_unlock_irqrestore(&pml4->lock, rflags);
    mapped_pml4 = pml4;

	serial_printf("[PAGER] Loaded PML4 at Phys - 0x%llx\n", pml4_phys);
}
