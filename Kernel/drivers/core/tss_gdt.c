#include <aos_inttypes.h>
#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/core/tss_gdt.h>

#define RPL0 0
#define RPL1 1
#define RPL2 2
#define RPL3 3

#define SEGMENT_SELECTOR(index, rpl) (((index) << 3) | (rpl))

#define GDT_NULL_SELECTOR 0x00
#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_DATA_SELECTOR 0x18
#define GDT_USER_CODE_SELECTOR 0x20

// Present:1 DPL:2 (ring) Segment:1 Exec:1 DC:1 FLAG/RW:1 Accessed:1
#define GDT_ACCESS_KERNEL_CODE 0b10011010
#define GDT_ACCESS_KERNEL_DATA 0b10010010
#define GDT_ACCESS_USER_CODE 0b11111010
#define GDT_ACCESS_USER_DATA 0b11110010
#define GDT_ACCESS_TSS 0x89

// G:1 DB:1	L:1	Reserved:1
#define GDT_FLAG_LONG_MODE 0b00100000
#define GDT_FLAG_PROTECTED_MODE 0b11000000
#define GDT_FLAG_NONE 0x0

#define GDT_INDEX_NULL 0
#define GDT_INDEX_KERNEL_CODE 1
#define GDT_INDEX_KERNEL_DATA 2
#define GDT_INDEX_USER_DATA 3
#define GDT_INDEX_USER_CODE 4
#define GDT_INDEX_TSS 5
#define GDT_ENTRY_SIZE 8
#define GDT_TSS_ENTRY_SIZE 16
#define GDT_NUM_ENTRIES 7

#define TSS_IO_BITMAP_DISABLED sizeof(tss_t)

#define KERNEL_CS SEGMENT_SELECTOR(GDT_INDEX_KERNEL_CODE, RPL0)
#define KERNEL_DS SEGMENT_SELECTOR(GDT_INDEX_KERNEL_DATA, RPL0)
#define USER_CS SEGMENT_SELECTOR(GDT_INDEX_USER_CODE, RPL3)
#define USER_DS SEGMENT_SELECTOR(GDT_INDEX_USER_DATA, RPL3)
#define GDT_TSS_SELECTOR SEGMENT_SELECTOR(GDT_INDEX_TSS, RPL0)

gdt_t kgdt = {0};
gdtr_t kgdtr = {0};
tss_t ktss = {0};

// TSS Stacks (160KB - Total , 16KB - Each)
char kstack[0x4000] __attribute__((aligned(0x1000)));
char kstack1[0x4000] __attribute__((aligned(0x1000)));
char kstack2[0x4000] __attribute__((aligned(0x1000)));

char kstack3[0x4000] __attribute__((aligned(0x1000)));
char kstack4[0x4000] __attribute__((aligned(0x1000)));
char kstack5[0x4000] __attribute__((aligned(0x1000)));
char kstack6[0x4000] __attribute__((aligned(0x1000)));
char kstack7[0x4000] __attribute__((aligned(0x1000)));
char kstack8[0x4000] __attribute__((aligned(0x1000)));
char kstack9[0x4000] __attribute__((aligned(0x1000)));

void gdt_init(void) {
	memset(&kgdtr, 0, sizeof(kgdtr));
	memset(&kgdt, 0, sizeof(kgdt));

	kgdt.kernel_code = (gdt_entry_t){
		.access = GDT_ACCESS_KERNEL_CODE,
		.limit_low = 0,
		.base_low = 0,
		.base_mid = 0,
		.base_high = 0,
		.granularity = GDT_FLAG_LONG_MODE
	};

	kgdt.kernel_data = (gdt_entry_t){
		.access = GDT_ACCESS_KERNEL_DATA,
		.limit_low = 0,
		.base_low = 0,
		.base_mid = 0,
		.base_high = 0,
		.granularity = 0
	};

	kgdt.user_code = (gdt_entry_t){
		.access = GDT_ACCESS_USER_CODE,
		.limit_low = 0,
		.base_low = 0,
		.base_mid = 0,
		.base_high = 0,
		.granularity = GDT_FLAG_LONG_MODE
	};

	kgdt.user_data = (gdt_entry_t){
		.access = GDT_ACCESS_USER_DATA,
		.limit_low = 0,
		.base_low = 0,
		.base_mid = 0,
		.base_high = 0,
		.granularity = 0
	};

	kgdt.tss = (gdt_tss_entry_t){
		.access = GDT_ACCESS_TSS,
		.limit_low = sizeof(tss_t)-1,
		.base_low = ((uint64_t)&ktss) & 0xFFFF,
		.base_mid1 = (((uint64_t)&ktss) >> 16) & 0xFF,
		.base_mid2 = (((uint64_t)&ktss) >> 24) & 0xFF,
		.base_high = ((uint64_t)&ktss) >> 32,
		.granularity = (((sizeof(tss_t)-1)>>16) & 0x0F),
	};

	kgdtr.limit = sizeof(kgdt)-1;
	kgdtr.base = (uint64_t)&kgdt;

	asm volatile(
		"lgdt %0\n\t"
		"mov %1, %%ax\n\t"
		"mov %%ax, %%ds\n\t"
		"mov %%ax, %%es\n\t"
		"mov %%ax, %%ss\n\t"
		"mov %2, %%rax\n\t"
		"pushq %%rax\n\t"
		"lea 1f(%%rip), %%rax\n\t"
		"pushq %%rax\n\t"
		"lretq\n\t"
		"1:\n\t"
		:
		:
		"m"(kgdtr), "i"(KERNEL_DS), "i"(KERNEL_CS)
		:
		"memory", "rax"
	);
}

void tss_init(void) {
	memset(&ktss, 0, sizeof(ktss));
	
	ktss.rsp0 = (uint64_t)(kstack + sizeof(kstack));
	ktss.rsp1 = (uint64_t)(kstack1 + sizeof(kstack1));
	ktss.rsp2 = (uint64_t)(kstack2 + sizeof(kstack2));

	ktss.ist1 = (uint64_t)(kstack3 + sizeof(kstack3));
	ktss.ist2 = (uint64_t)(kstack4 + sizeof(kstack4));
	ktss.ist3 = (uint64_t)(kstack5 + sizeof(kstack5));
	ktss.ist4 = (uint64_t)(kstack6 + sizeof(kstack6));
	ktss.ist5 = (uint64_t)(kstack7 + sizeof(kstack7));
	ktss.ist6 = (uint64_t)(kstack8 + sizeof(kstack8));
	ktss.ist7 = (uint64_t)(kstack9 + sizeof(kstack9));

	ktss.iomap_base = TSS_IO_BITMAP_DISABLED;

	asm volatile(
		"ltr %%ax"
		::
		"a"(GDT_TSS_SELECTOR)
	);
}