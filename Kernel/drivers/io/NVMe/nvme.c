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

	uint64_t mapping_size;
	volatile struct nvme_regs* regs;

	struct nvme_admin_queue admin_queue;
	struct nvme_io_queue io_queue;
	
	struct nvme_identify_controller* controller_identity;

	nvme_namespace namespaces[KNVME_MAX_NAMESPACES];
    uint32_t namespace_count;

	spinlock_t drive_lock;
} nvme_controller;

static nvme_controller* controllers;
static uint64_t controller_count;
static uint64_t controller_cap;

static void nvme_destroy(nvme_controller* knc) {
	if (!knc) return;

	if (knc->namespace_count > 0) {
		for (uint32_t i = 0; i < knc->namespace_count; i++) {
			if (i >= KNVME_MAX_NAMESPACES) break;
			nvme_namespace* ns = &knc->namespaces[i];
			if (!ns->valid) continue;
			if (ns->identity) {
				avmf_free((uint64_t)ns->identity);
				ns->identity = NULL;
			}
			ns->valid = AOS_FALSE;
		}
	}

	if (knc->controller_identity) {
		avmf_free((uint64_t)knc->controller_identity);
		knc->controller_identity = NULL;
	}
	
	if (knc->io_queue.cq) {
		avmf_free((uint64_t)knc->io_queue.cq);
		knc->io_queue.cq = NULL;
	}
	if (knc->io_queue.sq) {
		avmf_free((uint64_t)knc->io_queue.sq);
		knc->io_queue.sq = NULL;
	}
	if (knc->admin_queue.cq) {
		avmf_free((uint64_t)knc->admin_queue.cq);
		knc->admin_queue.cq = NULL;
	}
	if (knc->admin_queue.sq) {
		avmf_free((uint64_t)knc->admin_queue.sq);
		knc->admin_queue.sq = NULL;
	}

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

	uint64_t required_mapping = PAGE_SIZE + ((2ULL * NVME_IO_QUEUE_ID + 2ULL) * doorbell_stride);
	if (knc->mapping_size < required_mapping) {
		serial_printf("[NVMe] BAR too small for doorbells: BAR=0x%llX Required=0x%llX\n", knc->mapping_size, required_mapping);
		return AOS_FALSE;
	}

	knc->admin_queue.sq_db = (volatile uint32_t*)((uintptr_t)knc->regs + PAGE_SIZE);
	knc->admin_queue.cq_db = (volatile uint32_t*)((uintptr_t)knc->regs + PAGE_SIZE + doorbell_stride);
	knc->io_queue.sq_db = (volatile uint32_t*)((uintptr_t)knc->regs + PAGE_SIZE + ((2*NVME_IO_QUEUE_ID)*doorbell_stride));
	knc->io_queue.cq_db = (volatile uint32_t*)((uintptr_t)knc->regs + PAGE_SIZE + ((2*NVME_IO_QUEUE_ID+1)*doorbell_stride));

	serial_printf("[NVMe] Mapped all NVMe (Size: 0x%llx)\n", knc->mapping_size);
	return AOS_TRUE;
}

static aos_bool nvme_setup_admin_queue(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

	uint32_t max_queue_entries = NVME_CAP_MQES(knc->regs->cap) + 1;
	if (NVME_ADMIN_QUEUE_SIZE > max_queue_entries) {
		serial_printf("[NVMe] Admin queue size (%llu entries) exceeds controller maximum (%llu entries)\n", NVME_ADMIN_QUEUE_SIZE, max_queue_entries);
		return AOS_FALSE;
	}

	uint32_t page_shift = 12; // PAGE_SIZE == 4096
	if (page_shift < 12 + NVME_CAP_MPSMIN(knc->regs->cap) || page_shift > 12 + NVME_CAP_MPSMAX(knc->regs->cap)) {
		serial_print("[NVMe] Controller does not support kernel page size\n");
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

static aos_bool nvme_send_admin_cmd(nvme_controller* knc, struct nvme_command* cmd, uint16_t* out_cid) {
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

static void nvme_admin_notify_device(nvme_controller* knc) {
    if (!knc) return;

    struct nvme_admin_queue* aq = &knc->admin_queue;
    if (!aq->sq_db) return;

    *aq->sq_db = aq->sq_tail;
    __asm__ volatile("mfence" ::: "memory");
}

static aos_bool nvme_admin_poll_cmd_sync(nvme_controller* knc, uint16_t cid, struct nvme_completion* out_cqe) {
    if (!knc || !out_cqe) return AOS_FALSE;

    struct nvme_admin_queue* aq = &knc->admin_queue;
    if (!aq->cq || !aq->cq_db) return AOS_FALSE;

    uint64_t timeout = kget_ms_passed();
    while (1) {
        struct nvme_completion* cqe = &aq->cq[aq->cq_head];

        uint16_t status = cqe->status;
        if (NVME_CQE_PHASE(status) == aq->cq_phase) {
            __asm__ volatile("mfence" ::: "memory");

            uint16_t completed_cid = cqe->cid;
			struct nvme_completion completed;
			memcpy(&completed, cqe, sizeof(completed));

			aq->cq_head++;
			if (aq->cq_head >= NVME_ADMIN_QUEUE_SIZE) {
				aq->cq_head = 0;
				aq->cq_phase ^= 1U;
			}
			if (aq->sq_count > 0) aq->sq_count--;
			aq->cid_bitmap[0] &= ~(1ULL << completed_cid);

			*aq->cq_db = aq->cq_head;
			__asm__ volatile("mfence" ::: "memory");

            if (completed_cid == cid) {
                memcpy(out_cqe, &completed, sizeof(completed));
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

static aos_bool nvme_admin_cmd_sync(nvme_controller* knc, struct nvme_command* cmd, struct nvme_completion* out_cqe) {
    if (!knc || !cmd || !out_cqe) return AOS_FALSE;

    uint16_t cid;
    if (!nvme_send_admin_cmd(knc, cmd, &cid)) return AOS_FALSE;
    nvme_admin_notify_device(knc);

    if (!nvme_admin_poll_cmd_sync(knc, cid, out_cqe)) return AOS_FALSE;
    return AOS_TRUE;
}

static aos_bool nvme_create_io_cq(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

    struct nvme_command cmd = {0};
    struct nvme_completion cqe = {0};

    cmd.cdw0 = NVME_ADMIN_CMD_CREATE_CQ;
    cmd.prp1 = knc->io_queue.cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | NVME_IO_QUEUE_ID;
    cmd.cdw11 = NVME_CQ_PC;

    if (!nvme_admin_cmd_sync(knc, &cmd, &cqe)) return AOS_FALSE;
    if (NVME_CQE_STATUS(cqe.status) != NVME_SC_SUCCESS) return AOS_FALSE;

    return AOS_TRUE;
}

static aos_bool nvme_create_io_sq(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;

    struct nvme_command cmd = {0};
    struct nvme_completion cqe = {0};

    cmd.cdw0 = NVME_ADMIN_CMD_CREATE_SQ;
    cmd.prp1 = knc->io_queue.sq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | NVME_IO_QUEUE_ID;
    cmd.cdw11 = NVME_IO_QUEUE_ID | NVME_SQ_PRIO_URGENT | NVME_SQ_PC;

    if (!nvme_admin_cmd_sync(knc, &cmd, &cqe)) return AOS_FALSE;
    if (NVME_CQE_STATUS(cqe.status) != NVME_SC_SUCCESS) return AOS_FALSE;

    return AOS_TRUE;
}

static aos_bool nvme_setup_io_queue(nvme_controller* knc) {
	if (!knc) return AOS_FALSE;
	
	uint32_t max_queue_entries = NVME_CAP_MQES(knc->regs->cap) + 1;
	if (NVME_IO_QUEUE_SIZE > max_queue_entries) {
		serial_printf("[NVMe] IO queue size (%u entries) exceeds controller maximum (%u entries)\n", NVME_IO_QUEUE_SIZE, max_queue_entries);
		return AOS_FALSE;
	}

	uint32_t page_shift = 12; // PAGE_SIZE == 4096
	if (page_shift < 12 + NVME_CAP_MPSMIN(knc->regs->cap) || page_shift > 12 + NVME_CAP_MPSMAX(knc->regs->cap)) {
		serial_print("[NVMe] Controller does not support kernel page size\n");
		return AOS_FALSE;
	}

	knc->io_queue.sq = (struct nvme_command*)avmf_alloc(NVME_IO_QUEUE_SIZE*sizeof(struct nvme_command), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->io_queue.sq_phys);
	if (!knc->io_queue.sq || !knc->io_queue.sq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Submission Queue!\n");
		return AOS_FALSE;
	}

	knc->io_queue.cq = (struct nvme_completion*)avmf_alloc(NVME_IO_QUEUE_SIZE*sizeof(struct nvme_completion), MALLOC_TYPE_DRIVER, AVMF_FLAG_NO_CACHE | AVMF_FLAG_RW, &knc->io_queue.cq_phys);
	if (!knc->io_queue.cq || !knc->io_queue.cq_phys) {
		serial_print("[NVMe] Failed to allocate Admin Completion Queue!\n");
		avmf_free((uint64_t)knc->io_queue.sq);
		knc->io_queue.sq = NULL;

		return AOS_FALSE;
	}

	memset(knc->io_queue.sq, 0, NVME_IO_QUEUE_SIZE*sizeof(struct nvme_command));
    memset(knc->io_queue.cq, 0, NVME_IO_QUEUE_SIZE*sizeof(struct nvme_completion));

	knc->io_queue.sq_tail = 0;
	knc->io_queue.sq_count = 0;
    knc->io_queue.cq_head = 0;
    knc->io_queue.cq_phase = 1;
	knc->io_queue.next_cid = 0;
	knc->io_queue.cid_bitmap[0] = 0;

	if (!nvme_create_io_cq(knc)) {
		serial_print("[NVMe] Failed to create IO Completion Queue!\n");
		
		avmf_free((uint64_t)knc->io_queue.sq);
		knc->io_queue.sq = NULL;
		avmf_free((uint64_t)knc->io_queue.cq);
		knc->io_queue.cq = NULL;

		return AOS_FALSE;
	}

	if (!nvme_create_io_sq(knc)) {
		serial_print("[NVMe] Failed to create IO Submission Queue!\n");
		
		avmf_free((uint64_t)knc->io_queue.sq);
		knc->io_queue.sq = NULL;
		avmf_free((uint64_t)knc->io_queue.cq);
		knc->io_queue.cq = NULL;

		return AOS_FALSE;
	}

	serial_print("[NVMe] IO Queues Created!\n");
	return AOS_TRUE;
}

static aos_bool nvme_send_io_cmd(nvme_controller* knc, struct nvme_command* cmd, uint16_t* out_cid) {
    if (!knc || !cmd || !out_cid) return AOS_FALSE;

    struct nvme_io_queue* ioq = &knc->io_queue;
    if (!ioq->sq) return AOS_FALSE;
	if (ioq->sq_count >= NVME_IO_QUEUE_SIZE) return AOS_FALSE;

    uint16_t cid = ioq->next_cid;
	aos_bool found = AOS_FALSE;
    for (uint16_t i = 0; i < NVME_IO_QUEUE_SIZE; i++) {
        uint16_t candidate = (uint16_t)((cid + i) % NVME_IO_QUEUE_SIZE);

        uint64_t mask = 1ULL << candidate;
        if ((ioq->cid_bitmap[0] & mask) == 0) {
            cid = candidate;
            ioq->cid_bitmap[0] |= mask;
            
			found = AOS_TRUE;
			break;
        }
    }
	if (!found) return AOS_FALSE;

	ioq->next_cid = (uint16_t)((cid + 1U) % NVME_IO_QUEUE_SIZE);

	// CDW0[31:16] = CID
	uint16_t slot = ioq->sq_tail;
	struct nvme_command* sq_cmd = &ioq->sq[slot];
	memcpy(sq_cmd, cmd, sizeof(*sq_cmd));

	sq_cmd->cdw0 &= 0x0000FFFFU;
	sq_cmd->cdw0 |= ((uint32_t)cid << 16);

	ioq->sq_tail++;
	if (ioq->sq_tail >= NVME_IO_QUEUE_SIZE) ioq->sq_tail = 0;
	ioq->sq_count++;

	__asm__ volatile("mfence" ::: "memory");

	*out_cid = cid;

	return AOS_TRUE;
}

static void nvme_io_notify_device(nvme_controller* knc) {
    if (!knc) return;

    struct nvme_io_queue* ioq = &knc->io_queue;
    if (!ioq->sq_db) return;

    *ioq->sq_db = ioq->sq_tail;
    __asm__ volatile("mfence" ::: "memory");
}

static aos_bool nvme_io_poll_cmd_sync(nvme_controller* knc, uint16_t cid, struct nvme_completion* out_cqe) {
    if (!knc || !out_cqe) return AOS_FALSE;

    struct nvme_io_queue* ioq = &knc->io_queue;
    if (!ioq->cq || !ioq->cq_db) return AOS_FALSE;

    uint64_t timeout = kget_ms_passed();
    while (1) {
        struct nvme_completion* cqe = &ioq->cq[ioq->cq_head];

        uint16_t status = cqe->status;
        if (NVME_CQE_PHASE(status) == ioq->cq_phase) {
            __asm__ volatile("mfence" ::: "memory");

            uint16_t completed_cid = cqe->cid;
            struct nvme_completion completed;
            memcpy(&completed, cqe, sizeof(completed));

            ioq->cq_head++;
			if (ioq->cq_head >= NVME_IO_QUEUE_SIZE) {
				ioq->cq_head = 0;
				ioq->cq_phase ^= 1U;
			}
			if (ioq->sq_count > 0) ioq->sq_count--;
			ioq->cid_bitmap[0] &= ~(1ULL << completed_cid);

			*ioq->cq_db = ioq->cq_head;
			__asm__ volatile("mfence" ::: "memory");

            if (completed_cid == cid) {
                memcpy(out_cqe, &completed, sizeof(completed));
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

static aos_bool nvme_io_cmd_sync(nvme_controller* knc, struct nvme_command* cmd, struct nvme_completion* out_cqe) {
    if (!knc || !cmd || !out_cqe) return AOS_FALSE;

    uint16_t cid;
    if (!nvme_send_io_cmd(knc, cmd, &cid)) return AOS_FALSE;
    nvme_io_notify_device(knc);

    if (!nvme_io_poll_cmd_sync(knc, cid, out_cqe)) return AOS_FALSE;
    return AOS_TRUE;
}

static aos_bool nvme_identify_cnt(nvme_controller* knc, struct nvme_identify_controller** out) {
	if (!knc || !out) return AOS_FALSE;

	struct nvme_command cmd = {0};
	struct nvme_completion cqe;

	uint64_t iden_phys = 0;
	struct nvme_identify_controller* iden = (struct nvme_identify_controller*)avmf_alloc(ALIGN_UP(sizeof(struct nvme_identify_controller), PAGE_SIZE), MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, &iden_phys);
	if (!iden_phys || !iden) return AOS_FALSE;

	memset(iden, 0, ALIGN_UP(sizeof(struct nvme_identify_controller), PAGE_SIZE));

	cmd.cdw0 = NVME_ADMIN_CMD_IDENTIFY;
	cmd.prp1 = iden_phys;
	cmd.cdw10 = NVME_IDENTIFY_CNS_CONTROLLER;

	if (!nvme_admin_cmd_sync(knc, &cmd, &cqe)) {
		avmf_free((uint64_t)iden);
		return AOS_FALSE;
	}

	if (NVME_CQE_STATUS(cqe.status) != NVME_SC_SUCCESS) {
		avmf_free((uint64_t)iden);
		return AOS_FALSE;
	}

	*out = iden;
	return AOS_TRUE;
}

static aos_bool nvme_identify_namespace(nvme_controller* knc, uint32_t nsid, struct nvme_identify_namespace** out) {
	if (!knc || !out) return AOS_FALSE;

	struct nvme_command cmd = {0};
	struct nvme_completion cqe;

	uint64_t iden_phys = 0;
	struct nvme_identify_namespace* iden = (struct nvme_identify_namespace*)avmf_alloc(ALIGN_UP(sizeof(struct nvme_identify_namespace), PAGE_SIZE), MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, &iden_phys);
	if (!iden_phys || !iden) return AOS_FALSE;

	memset(iden, 0, ALIGN_UP(sizeof(struct nvme_identify_namespace), PAGE_SIZE));

	cmd.cdw0 = NVME_ADMIN_CMD_IDENTIFY;
	cmd.prp1 = iden_phys;
	cmd.cdw10 = NVME_IDENTIFY_CNS_NAMESPACE;
	cmd.nsid = nsid;

	if (!nvme_admin_cmd_sync(knc, &cmd, &cqe)) {
		avmf_free((uint64_t)iden);
		return AOS_FALSE;
	}

	if (NVME_CQE_STATUS(cqe.status) != NVME_SC_SUCCESS) {
		avmf_free((uint64_t)iden);
		return AOS_FALSE;
	}

	*out = iden;
	return AOS_TRUE;
}

aos_bool nvme_init(struct AOS_Module* m) {
	if (!m) return AOS_FALSE;
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

	if (!nvme_setup_io_queue(knc)) {
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	if (!nvme_identify_cnt(knc, &knc->controller_identity)) {
		serial_print("[NVMe] Failed to identify device!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	if (knc->controller_identity->nn < 1) {
		serial_print("[NVMe] Controller has no namespaces!\n");
		nvme_destroy(knc);
		return AOS_FALSE;
	}

	for (uint32_t i = 1; i <= knc->controller_identity->nn; i++) {
		if (i > KNVME_MAX_NAMESPACES) {
			serial_print("[NVMe] Controller has too many namespaces to register, skipping extra.\n");
			break;
		}
		nvme_namespace* ns = &knc->namespaces[knc->namespace_count++];
		memset(ns, 0, sizeof(nvme_namespace));

		if (!nvme_identify_namespace(knc, i, &ns->identity)) {
			serial_printf("[NVMe] Failed to identify namespace %u!\n", i);
			nvme_destroy(knc);
			return AOS_FALSE;
		}
		ns->valid = AOS_TRUE;
		ns->nsid = i;
	}

	knc->valid = AOS_TRUE;
	return AOS_TRUE;
}

aos_bool nvme_get_pcie(uint64_t cidx, pcie_device_t* out) {
	if (!out) return AOS_FALSE;
	if (cidx >= controller_count) return AOS_FALSE;
	nvme_controller* knc = &controllers[cidx];
	if (!knc->valid) return AOS_FALSE;

    memcpy(out, &knc->nvme_device, sizeof(pcie_device_t));

	return AOS_TRUE;
}

aos_bool nvme_get_block_device(uint64_t cidx, uint64_t port_id, struct block_device* out) {
	if (!out) return AOS_FALSE;
	if (cidx >= controller_count) return AOS_FALSE;
	nvme_controller* knc = &controllers[cidx];
	if (!knc->valid || !knc->controller_identity || knc->namespace_count < 1 || port_id >= knc->namespace_count) return AOS_FALSE;

	nvme_namespace* ns = &knc->namespaces[port_id];
	if (!ns->valid || !ns->identity) return AOS_FALSE;

	out->block_count = ns->identity->nsze;
	uint8_t flbas = ns->identity->flbas & 0x0F;
	out->block_size = 1U << ns->identity->lbaf[flbas].ds;

	char* name = (char*)avmf_alloc(80 * sizeof(char), MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, NULL);
	if (!name) return AOS_FALSE;
	memset(name, 0, 80 * sizeof(char));

	memcpy(name, "nvme", 4);
	ku64_to_str(cidx, (char*)((uint8_t*)name + 4), 10, AOS_FALSE);

	size_t len = strlen(name);
	if (len+1 > 80*sizeof(char)) {
		avmf_free((uint64_t)name);
		return AOS_FALSE;
	}
	name[len++] = 'n';
	ku64_to_str(ns->nsid, (char*)((uint8_t*)name + len), 10, AOS_FALSE);

	out->name = name;
	return AOS_TRUE;
}

aos_bool nvme_get_available_ports(uint64_t cidx, uint8_t* out, uint64_t out_size) {
	if (!out) return AOS_FALSE;
	if (out_size == 0) return AOS_TRUE;
	
	if (cidx >= controller_count) return AOS_FALSE;
	nvme_controller* knc = &controllers[cidx];
	if (!knc->valid || knc->namespace_count < 1) return AOS_FALSE;

    for (uint64_t i = 0; i < knc->namespace_count; i++) {
        if (i + 1 > out_size || i >= KNVME_MAX_NAMESPACES) break;
        out[i] = knc->namespaces[i].valid;
    }

	return AOS_TRUE;
}
