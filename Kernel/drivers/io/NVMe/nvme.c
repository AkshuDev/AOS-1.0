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

#define KNVME_MAX_NAMESPACES 32
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

	nvme_namespace namespaces[KNVME_MAX_NAMESPACES];
    uint32_t namespace_count;

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

	uint32_t doorbell_stride = 4U << NVME_CAP_DSTRD(knc->regs->cap);
	knc->admin_queue.sq_db = (volatile uint32_t*)((uintptr_t)knc->regs + 0x1000);
	knc->admin_queue.cq_db = (volatile uint32_t*)((uintptr_t)knc->regs + 0x1000 + doorbell_stride);

	serial_printf("[NVMe] Mapped all NVMe (Size: 0x%llx)\n", knc->mapping_size);
	return AOS_TRUE;
}

static aos_bool nvme_setup_admin_queue(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

	uint32_t page_shift = 12; // PAGE_SIZE == 4096
	if (page_shift < 12 + NVME_CAP_MPSMIN(knc->regs->cap) || page_shift > 12 + NVME_CAP_MPSMAX(knc->regs->cap)) {
		serial_print("[NVMe] Controller does not support kernel page size\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	knc->admin_queue.sq = (struct nvme_command*)avmf_alloc(NVME_ADMIN_QUEUE_SIZE*sizeof(struct nvme_command), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->admin_queue.sq_phys);
	if (!knc->admin_queue.sq || !knc->admin_queue.sq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Submission Queue!\n");
		return AOS_FALSE;
	}

	knc->admin_queue.cq = (struct nvme_completion*)avmf_alloc(NVME_ADMIN_QUEUE_SIZE*sizeof(struct nvme_completion), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->admin_queue.cq_phys);
	if (!knc->admin_queue.cq || !knc->admin_queue.cq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Completion Queue!\n");
		avmf_free((uint64_t)knc->admin_queue.sq);
		knc->admin_queue.sq = NULL;

		return AOS_FALSE;
	}

	memset(knc->admin_queue.sq, 0, NVME_ADMIN_QUEUE_SIZE*sizeof(struct nvme_command));
    memset(knc->admin_queue.cq, 0, NVME_ADMIN_QUEUE_SIZE*sizeof(struct nvme_completion));

	knc->admin_queue.sq_tail = 0;
	knc->admin_queue.sq_count = 0;
    knc->admin_queue.cq_head = 0;
    knc->admin_queue.cq_phase = 1;
	knc->admin_queue.next_cid = 0;
	knc->admin_queue.cid_bitmap[0] = 0;

	knc->regs->aqa &= ~NVME_AQA_ASQS_MASK;
	knc->regs->aqa |= ((NVME_ADMIN_QUEUE_SIZE-1) << NVME_AQA_ASQS_SHIFT) & NVME_AQA_ASQS_MASK;
	
	knc->regs->aqa &= ~NVME_AQA_ACQS_MASK;
	knc->regs->aqa |= ((NVME_ADMIN_QUEUE_SIZE-1) << NVME_AQA_ACQS_SHIFT) & NVME_AQA_ACQS_MASK;
	
	knc->regs->asq = knc->admin_queue.sq_phys;
	knc->regs->acq = knc->admin_queue.cq_phys;
	__asm__ volatile("mfence" ::: "memory");
	
	knc->regs->cc &= ~NVME_CC_CSS_MASK;
	knc->regs->cc &= ~NVME_CC_AMS_MASK;
	
	knc->regs->cc &= ~NVME_CC_MPS_MASK;
	knc->regs->cc |= ((page_shift - 12) << NVME_CC_MPS_SHIFT) & NVME_CC_MPS_MASK;
	
	knc->regs->cc &= ~NVME_CC_IOSQES_MASK;
	knc->regs->cc |= (6 << NVME_CC_IOSQES_SHIFT) & NVME_CC_IOSQES_MASK;

	knc->regs->cc &= ~NVME_CC_IOCQES_MASK;
	knc->regs->cc |= (4 << NVME_CC_IOCQES_SHIFT) & NVME_CC_IOCQES_MASK;
	__asm__ volatile("mfence" ::: "memory");

	serial_print("[NVMe] Admin Queues Created!\n");
	return AOS_TRUE;
}

static aos_bool nvme_send_cmd(nvme_controller* knc, struct nvme_command* cmd, uint16_t* out_cid) {
    if (!knc || !cmd || !out_cid) return AOS_FALSE;

    struct nvme_admin_queue* aq = &knc->admin_queue;
    if (!aq->sq) return AOS_FALSE;
	if (aq->sq_count >= NVME_ADMIN_QUEUE_SIZE) return AOS_FALSE;

    uint16_t cid = aq->next_cid;
	aos_bool found = AOS_FALSE;
    for (uint16_t i = 0; i < NVME_ADMIN_QUEUE_SIZE; i++) {
        uint16_t candidate = (uint16_t)((cid + i) % NVME_ADMIN_QUEUE_SIZE);

        uint64_t mask = 1ULL << candidate;
        if ((aq->cid_bitmap[0] & mask) == 0) {
            cid = candidate;
            aq->cid_bitmap[0] |= mask;
            
			found = AOS_TRUE;
			break;
        }
    }
	if (!found) return AOS_FALSE;

	aq->next_cid = (uint16_t)((cid + 1U) % NVME_ADMIN_QUEUE_SIZE);

	// CDW0[31:16] = CID
	uint16_t slot = aq->sq_tail;
	struct nvme_command* sq_cmd = &aq->sq[slot];
	memcpy(sq_cmd, cmd, sizeof(*sq_cmd));

	sq_cmd->cdw0 &= 0x0000FFFFU;
	sq_cmd->cdw0 |= ((uint32_t)cid << 16);

	aq->sq_tail++;
	if (aq->sq_tail >= NVME_ADMIN_QUEUE_SIZE) aq->sq_tail = 0;
	aq->sq_count++;

	__asm__ volatile("mfence" ::: "memory");

	*out_cid = cid;

	return AOS_TRUE;
}

static void nvme_notify_device(nvme_controller* knc) {
    if (!knc) return;

    struct nvme_admin_queue* aq = &knc->admin_queue;
    if (!aq->sq_db) return;

    *aq->sq_db = aq->sq_tail;
    __asm__ volatile("mfence" ::: "memory");
}

static aos_bool nvme_poll_cmd_sync(nvme_controller* knc, uint16_t cid, struct nvme_completion* out_cqe) {
    if (!knc || !out_cqe) return AOS_FALSE;

    struct nvme_admin_queue* aq = &knc->admin_queue;
    if (!aq->cq || !aq->cq_db) return AOS_FALSE;

    uint64_t timeout = kget_ms_passed();
    while (1) {
        struct nvme_completion* cqe = &aq->cq[aq->cq_head];

        uint16_t status = cqe->status;
        if ((status & 0x1U) == aq->cq_phase) {
            __asm__ volatile("mfence" ::: "memory");

            uint16_t completed_cid = cqe->cid;
            if (completed_cid == cid) {
                memcpy(out_cqe, cqe, sizeof(*out_cqe));

                aq->cq_head++;
                if (aq->cq_head >= NVME_ADMIN_QUEUE_SIZE) {
                    aq->cq_head = 0;
                    aq->cq_phase ^= 1U;
                }
				if (aq->sq_count > 0) aq->sq_count--;
				aq->cid_bitmap[0] &= ~(1ULL << completed_cid);

				*aq->cq_db = aq->cq_head;
                __asm__ volatile("mfence" ::: "memory");

                return AOS_TRUE;
            }
        }

        if (NVME_CSTS_CFS(knc->regs->csts)) {
            serial_print("[NVMe] Controller Fatal Status!\n");
            return AOS_FALSE;
        }

        if (kget_ms_passed() - timeout > 10000) {
            serial_print("[NVMe] Command Completion Timeout!\n");
            return AOS_FALSE;
        }
    }
}

static aos_bool nvme_cmd_sync(nvme_controller* knc, struct nvme_command* cmd, struct nvme_completion* out_cqe) {
    if (!knc || !cmd || !out_cqe) return AOS_FALSE;

    uint16_t cid;
    if (!nvme_send_cmd(knc, cmd, &cid)) return AOS_FALSE;
    nvme_notify_device(knc);

    if (!nvme_poll_cmd_sync(knc, cid, out_cqe)) return AOS_FALSE;
    return AOS_TRUE;
}

static aos_bool nvme_identify_controller(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

	struct nvme_command cmd = {0};
	struct nvme_completion cqe;

	// cmd.cdw0 = NVME_ADMIN_CMD_IDENTIFY;
	// cmd.prp1 = identify_phys;
	// cmd.cdw10 = 0x01;

	if (!nvme_cmd_sync(knc, &cmd, &cqe))
		return AOS_FALSE;
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
	knc->regs->cc &= ~NVME_CC_EN_MASK;
	__asm__ volatile("mfence" ::: "memory");

    uint64_t timeout = kget_ms_passed();
	while (NVME_CSTS_RDY(knc->regs->csts) != 0) {
		if (kget_ms_passed() - timeout > 10000) {
			serial_print("[NVMe] Controller Disable Timeout!\n");
			nvme_destroy(knc);
			return AOS_FALSE;
		}
	}
	if (NVME_CSTS_CFS(knc->regs->csts)) {
		serial_print("[NVMe] Controller Fatal Status!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	if (!nvme_setup_admin_queue(knc)) {
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	knc->regs->cc |= NVME_CC_EN_MASK;
	__asm__ volatile("mfence" ::: "memory");

	timeout = kget_ms_passed();
	while (NVME_CSTS_RDY(knc->regs->csts) == 0) {
		if (kget_ms_passed() - timeout > 10000) {
			serial_print("[NVMe] Controller Enable Timeout!\n");
			nvme_destroy(knc);
			return AOS_FALSE;
		}
	}
	if (NVME_CSTS_CFS(knc->regs->csts)) {
		serial_print("[NVMe] Controller Fatal Status!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	return AOS_TRUE;
}
