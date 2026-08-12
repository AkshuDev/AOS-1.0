#include <system.h>
#include <aos_inttypes.h>
#include <asm.h>

#include <inc/core/module.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/pcie.h>
#include <inc/core/kfuncs.h>

#include <inc/drivers/io/nvme.h>
#include <inc/drivers/io/io.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

#define NVMe_ADMIN_QUEUE_SIZE 64

#define KNVME_MAX_PORTS 32
#define KNVME_ALLOC_STEP 16
#define KNVME_MAX_CONTROLLERS 256

typedef struct {
	uint64_t idx;
	aos_bool valid;

	pcie_device_t nvme_device;
	aos_bool found_nvme;

	uint64_t mapping_size;
	volatile struct nvme_regs* regs;

	struct nvme_admin_queue admin_queue;

	spinlock_t drive_lock;
} nvme_controller;

static nvme_controller* controllers;
static uint64_t controller_count;
static uint64_t controller_cap;

static void nvme_destroy(nvme_controller* knc) {
	if (!knc) return;

	if (knc->admin_queue.cq) {
		avmf_free((uint64_t)knc->admin_queue.cq);
		knc->admin_queue.cq = NULL;
	}
	if (knc->admin_queue.sq) {
		avmf_free((uint64_t)knc->admin_queue.sq);
		knc->admin_queue.sq = NULL;
	}

	knc->found_nvme = AOS_FALSE;
	knc->valid = AOS_FALSE;
	if (knc->idx == controller_count-1) controller_count--;
}

static aos_bool nvme_map_bar(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

    uint8_t bus = knc->nvme_device.bus;
    uint8_t slot = knc->nvme_device.slot;
    uint8_t func = knc->nvme_device.func;

    uint32_t bar = pcie_read_bar(bus, slot, func, 0);
    uint64_t bar_phys = bar & ~0xFULL;
	if ((bar_phys & 0xFFF) != 0) { // BAR should be page-aligned
		serial_print("[NVMe] BAR0 is not page-aligned, mapping failed!\n");
		return AOS_FALSE;
	}

    aos_bool is_64bit = ((bar >> 1) & 0b011) == 0x2;
	uint32_t orig0 = bar;
    uint32_t orig1 = 0;

	if ((bar & 0b001) == 0x1) {
		serial_print("[NVMe] BAR0 is not a memory BAR, mapping failed!\n");
		return AOS_FALSE;
	}

    if (is_64bit) {
        orig1 = pcie_read_bar(bus, slot, func, 1);
        bar_phys |= ((uint64_t)orig1 << 32);
    }

	pcie_toggle_memory_space(bus, slot, func, AOS_FALSE);
	
    pcie_write_bar(bus, slot, func, 0, 0xFFFFFFFF);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read_bar(bus, slot, func, 0);
    uint32_t mask1 = is_64bit ? pcie_read_bar(bus, slot, func, 1) : 0;

    pcie_write_bar(bus, slot, func, 0, orig0);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, orig1);

	pcie_toggle_memory_space(bus, slot, func, AOS_TRUE);

	uint64_t size = 0;
	if (is_64bit) {
		uint64_t mask = (mask0 & ~0xFULL) | ((uint64_t)mask1 << 32);
		size = ~mask + 1;
	} else {
		uint32_t mask = (uint32_t)(mask0 & ~0xFULL);
		size = (uint32_t)(~mask + 1);
	}

    knc->mapping_size = size;
	if (knc->mapping_size < sizeof(struct nvme_regs)) {
		serial_print("[NVMe] Device reported memory size is lower than minimum, mapping failed!\n");
		return AOS_FALSE;
	}

	pager_map_range(AOS_DIRECT_MAP_BASE + bar_phys, bar_phys, knc->mapping_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	pcie_toggle_busmaster(bus, slot, func, AOS_TRUE);

	knc->regs = (volatile struct nvme_regs*)(AOS_DIRECT_MAP_BASE + bar_phys);
	serial_printf("[NVMe] Mapped all NVMe (Size: 0x%llx)\n", knc->mapping_size);
	return AOS_TRUE;
}

static aos_bool nvme_setup_admin_queue(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

	uint32_t page_shift = 12; // PAGE_SIZE == 4096
	if (page_shift < 12 + knc->regs->cap.mpsmin || page_shift > 12 + knc->regs->cap.mpsmax) {
		serial_print("[NVMe] Controller does not support kernel page size\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	knc->admin_queue.sq = (struct nvme_command*)avmf_alloc(NVMe_ADMIN_QUEUE_SIZE*sizeof(struct nvme_command), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->admin_queue.sq_phys);
	if (!knc->admin_queue.sq || !knc->admin_queue.sq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Submission Queue!\n");
		return AOS_FALSE;
	}

	knc->admin_queue.cq = (struct nvme_completion*)avmf_alloc(NVMe_ADMIN_QUEUE_SIZE*sizeof(struct nvme_completion), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->admin_queue.cq_phys);
	if (!knc->admin_queue.cq || !knc->admin_queue.cq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Completion Queue!\n");
		avmf_free((uint64_t)knc->admin_queue.sq);
		knc->admin_queue.sq = NULL;

		return AOS_FALSE;
	}

	memset(knc->admin_queue.sq, 0, NVMe_ADMIN_QUEUE_SIZE*sizeof(struct nvme_command));
    memset(knc->admin_queue.cq, 0, NVMe_ADMIN_QUEUE_SIZE*sizeof(struct nvme_completion));

	knc->admin_queue.sq_tail = 0;
    knc->admin_queue.cq_head = 0;
    knc->admin_queue.cq_phase = 1;

	knc->regs->aqa.asqs = NVMe_ADMIN_QUEUE_SIZE-1;
	knc->regs->aqa.acqs = NVMe_ADMIN_QUEUE_SIZE-1;
	knc->regs->asq = (volatile nvme_asq_t)knc->admin_queue.sq_phys;
	knc->regs->acq = (volatile nvme_acq_t)knc->admin_queue.cq_phys;
	__asm__ volatile("mfence" ::: "memory");
	
	knc->regs->cc.css = 0;
	knc->regs->cc.mps = page_shift - 12;
	knc->regs->cc.iosqes = 6;
	knc->regs->cc.iocqes = 4;
	knc->regs->cc.ams = 0;
	__asm__ volatile("mfence" ::: "memory");

	serial_print("[NVMe] Admin Queues Created!\n");
	return AOS_TRUE;
}

aos_bool nvme_init(struct AOS_Module* m) {
    if (m->hdr.type != MODULE_TYPE_DRIVER) return AOS_FALSE;
    if (m->Modules.driver_module.type != MODULE_DRIVER_TYPE_NVMe) return AOS_FALSE;

	if (!controllers) {
		controllers = (nvme_controller*)avmf_alloc(sizeof(nvme_controller) * KNVME_ALLOC_STEP, MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, NULL);
		if (!controllers) return AOS_FALSE;
		controller_cap = KNVME_ALLOC_STEP;
		controller_count = 0;
	} else if (controller_count >= controller_cap) {
		if (controller_count >= KNVME_MAX_CONTROLLERS) return AOS_FALSE;
		nvme_controller* nptr = (nvme_controller*)avmf_alloc(sizeof(nvme_controller) * (controller_cap + KNVME_ALLOC_STEP), MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, NULL);
		if (!nptr) return AOS_FALSE;
		memcpy(nptr, controllers, sizeof(nvme_controller)*controller_count);
		avmf_free((uint64_t)controllers);
		controllers = nptr;
		controller_cap += KNVME_ALLOC_STEP;
	}

	nvme_controller* knc = &controllers[controller_count];
	memset(knc, 0, sizeof(nvme_controller));
	knc->idx = controller_count;
	controller_count++;
	m->Modules.driver_module.DriverConnections.drive_connector.controller_idx = knc->idx;

    knc->nvme_device = m->Modules.driver_module.pcie_device;
	knc->valid = AOS_FALSE;

    if (!nvme_map_bar(knc)) {
		nvme_destroy(knc);
		return AOS_FALSE;
	}
    // Reset
	knc->regs->cc.en = 0;
	__asm__ volatile("mfence" ::: "memory");

    uint64_t timeout = kget_ms_passed();
	while (knc->regs->csts.rdy != 0) {
		if (kget_ms_passed() - timeout > 10000) {
			serial_print("[NVMe] Controller Disable Timeout!\n");
			nvme_destroy(knc);
			return AOS_FALSE;
		}
	}
	if (knc->regs->csts.cfs) {
		serial_print("[NVMe] Controller Fatal Status!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	if (!nvme_setup_admin_queue(knc)) {
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	knc->regs->cc.en = 1;
	__asm__ volatile("mfence" ::: "memory");

	timeout = kget_ms_passed();
	while (knc->regs->csts.rdy == 0) {
		if (kget_ms_passed() - timeout > 10000) {
			serial_print("[NVMe] Controller Enable Timeout!\n");
			nvme_destroy(knc);
			return AOS_FALSE;
		}
	}
	if (knc->regs->csts.cfs) {
		serial_print("[NVMe] Controller Fatal Status!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	return AOS_TRUE;
}
