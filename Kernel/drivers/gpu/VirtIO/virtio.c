#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/virtio.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/core/pcie.h>
#include <inc/core/smp.h>
#include <inc/core/module.h>
#include <inc/core/kfuncs.h>

#define KVIRTIO_ALLOC_STEP 16

#define MAX_CMD_RESP_BUFS 16

typedef struct {
	struct virtqueue virtq;
	aos_bool acceleration_present;

	struct virtio_gpu_ctrl_hdr* cmd_buf;
	struct virtio_gpu_resp_display_info* resp_buf;
	uint64_t cmd_buf_phys[MAX_CMD_RESP_BUFS];
	uint64_t resp_buf_phys[MAX_CMD_RESP_BUFS];
	uint64_t main_buf_slot;
	uint64_t worker_buf_slot;

	spinlock_t virtq_lock;

	uintptr_t notify_base;
	uint32_t notify_multiplier;

	uint32_t gpu_cmd_core;

	volatile struct virtio_common_cfg* common_cfg;
	uint64_t mapping_size;

	uint64_t idx;
	struct gpu_device* gpu;
	aos_bool valid;
} virtio_controller;

static virtio_controller* controllers;
static uint64_t controller_count;
static uint64_t controller_cap;

static void mmio_write64(uint64_t addr, uint64_t val) {
    *(volatile uint64_t*)addr = val;
}
static uint64_t mmio_read64(uint64_t addr) {
    return *(volatile uint64_t*)addr;
}
static void mmio_write32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}
static uint32_t mmio_read32(uint64_t addr) {
    return *(volatile uint32_t*)addr;
}
static void mmio_write16(uint64_t addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}
static uint16_t mmio_read16(uint64_t addr) {
    return *(volatile uint16_t*)addr;
}
static void mmio_write8(uint64_t addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}
static uint8_t mmio_read8(uint64_t addr) {
    return *(volatile uint8_t*)addr;
}

static aos_bool virtio_reset_device(virtio_controller* kvc) {
	if (!kvc) return AOS_FALSE;
	if (kvc->valid || kvc->gpu->active) return AOS_FALSE; // NEVER REINITIALIZE ON WORKING DEVICE
	
	kvc->common_cfg->device_status = 0;
	__asm__ volatile("mfence" ::: "memory");
	uint64_t timeout = kget_ms_passed();
    while (kvc->common_cfg->device_status != 0) {
		if (kget_ms_passed() - timeout > 10000) {
			serial_print("[VirtIO:GPU] Reset timed out!\n");
			return AOS_FALSE;
		}
		__asm__ volatile("pause");
	}
    kvc->common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
	__asm__ volatile("mfence" ::: "memory");
    kvc->common_cfg->device_status |= VIRTIO_STATUS_DRIVER;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);
	return AOS_TRUE;
}

static aos_bool virtio_negotiate_features(virtio_controller* kvc) {
	if (!kvc) return AOS_FALSE;
	if (kvc->valid || kvc->gpu->active) return AOS_FALSE; // NEVER REINITIALIZE ON WORKING DEVICE

	kvc->common_cfg->device_feature_select = 0;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);
  	uint32_t dev_lo = kvc->common_cfg->device_feature;

	kvc->common_cfg->device_feature_select = 1;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);
	uint32_t dev_hi = kvc->common_cfg->device_feature;

	uint64_t device_features = ((uint64_t)dev_hi << 32) | dev_lo;

	if (device_features == 0x0 || device_features == 0xFFFFFFFFFFFFFFFFULL) {
		serial_printf("[VirtIO:GPU] Invalid features provided! (0x%llx)\n", device_features);
		return AOS_FALSE;
	}

	uint64_t driver_features = 0;

	if (device_features & (1ULL << VIRTIO_F_VERSION_1)) driver_features |= 1ULL << VIRTIO_F_VERSION_1;
	else {
		serial_print("[VirtIO:GPU] Device failed to provide VIRTIO_F_VERSION_1\n");
		return AOS_FALSE;
	}
	if (device_features & (1ULL << VIRTIO_GPU_F_VIRGL)) {
		driver_features |= 1ULL << VIRTIO_GPU_F_VIRGL;
		kvc->gpu->acceleration_present = AOS_TRUE;
		kvc->acceleration_present = AOS_TRUE;
	} else {
		kvc->gpu->acceleration_present = AOS_FALSE;
		kvc->acceleration_present = AOS_FALSE;
	}

	kvc->common_cfg->driver_feature_select = 0;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);
	kvc->common_cfg->driver_feature = (uint32_t)driver_features;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);

	kvc->common_cfg->driver_feature_select = 1;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);

	kvc->common_cfg->driver_feature = (uint32_t)(driver_features >> 32);
	__asm__ volatile("mfence" ::: "memory");

    kvc->common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);
    if (!(kvc->common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_print("[VirtIO:GPU] Failed to negotiate features!\n");
        return AOS_FALSE;
    }
}

static aos_bool virtio_setup_queues(virtio_controller* kvc, uint16_t q_idx) {
	if (!kvc) return AOS_FALSE;
	if (kvc->valid || kvc->gpu->active) return AOS_FALSE; // NEVER REINITIALIZE ON WORKING DEVICE
	
	if (kvc->common_cfg->num_queues < 1) {
		serial_print("[VirtIO:GPU] Device has no queues, quiting...\n");
		return AOS_FALSE;
	}

    serial_print("[VirtIO:GPU] Setting Queues....\n");

    kvc->common_cfg->queue_select = q_idx;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);

	uint16_t size = kvc->common_cfg->queue_size;
	kvc->common_cfg->queue_msix_vector = 0xFFFF;
	kvc->common_cfg->queue_size = size;
	__asm__ volatile("mfence" ::: "memory");

	if (size < 1) {
		serial_print("[VirtIO:GPU] CommonCFG Queue Size is less than 1, quiting...\n");
		return AOS_FALSE;
	}

    size_t desc_t_size = ALIGN_UP(size * sizeof(struct virtq_desc), 16);
    size_t avail_r_size = ALIGN_UP(6 + (2 * size), 2);
    size_t used_r_size = ALIGN_UP(6 + (8 * size), 4);

    uint64_t phys = 0;
    void* mem = (void*)avmf_alloc(desc_t_size + avail_r_size + used_r_size, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &phys);
    if (!mem) {serial_print("[VirtIO:GPU] Failed to Allocate Memory!\n"); return AOS_FALSE;}
    if (!phys) {serial_print("[VirtIO:GPU] Failed to retrieve physical address!\n"); return AOS_FALSE;}

	memset(mem, 0, desc_t_size + avail_r_size + used_r_size);

    uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);
    kvc->virtq.desc = (volatile struct virtq_desc*)mem;
    kvc->virtq.avail = (volatile struct virtq_avail*)((uintptr_t)mem + desc_t_size);
    kvc->virtq.used = (volatile struct virtq_used*)((uintptr_t)kvc->virtq.avail + avail_r_size);
    kvc->virtq.queue_size = size;
    kvc->virtq.free_head = 0;
	kvc->virtq.last_used_idx = 0;

	uint64_t desc_count = desc_t_size / sizeof(struct virtq_desc);
	for (uint64_t i = 0; i < desc_count; i++) {
		if (i + 1 == desc_count) {
			kvc->virtq.desc[i].next = 0xFFFF;
			break;
		}
		kvc->virtq.desc[i].next = i + 1;
	}

    spin_unlock_irqrestore(&kvc->virtq_lock, flags);

    kvc->common_cfg->queue_desc = phys;
    kvc->common_cfg->queue_avail = phys + desc_t_size;
    kvc->common_cfg->queue_used = (uintptr_t)phys + avail_r_size + desc_t_size;
    kvc->common_cfg->queue_enable = 1;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);

	if (kvc->common_cfg->queue_enable != 1) {
		serial_print("[VirtIO:GPU] Device rejected queue!\n");
		return AOS_FALSE;
	}

	kvc->main_buf_slot = 0;

    serial_print("[VirtIO:GPU] Queues ready!\n");
	return AOS_TRUE;
}

static void virtio_destroy_queues(virtio_controller* kvc) {
	if (!kvc) return;
	if (kvc->virtq.desc) {
		while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
			__asm__ volatile("pause"); 
		}

		uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);
		avmf_free((uint64_t)kvc->virtq.desc);
		kvc->virtq.desc = NULL;
		spin_unlock_irqrestore(&kvc->virtq_lock, flags);
	}
}

static aos_bool virtio_setup_buffers(virtio_controller* kvc) {
	if (!kvc) return AOS_FALSE;
	if (kvc->valid || kvc->gpu->active) return AOS_FALSE; // NEVER REINITIALIZE ON WORKING DEVICE

	uint64_t cmd_phys = 0;
	uint64_t resp_phys = 0;
	kvc->cmd_buf = (struct virtio_gpu_ctrl_hdr*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &cmd_phys);
	kvc->resp_buf = (struct virtio_gpu_resp_display_info*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &resp_phys);
	if (!kvc->cmd_buf || !kvc->resp_buf || !cmd_phys || !resp_phys) {
		serial_print("[VirtIO:GPU] Failed to allocate command and response buffers!\n");
		return AOS_FALSE;
	}

	for (uint8_t i = 0; i < MAX_CMD_RESP_BUFS; i++) {
		kvc->cmd_buf_phys[i] = cmd_phys + (i * sizeof(struct virtio_gpu_ctrl_hdr));
		kvc->resp_buf_phys[i] = resp_phys + (i * sizeof(struct virtio_gpu_resp_display_info));
	}
}

static void virtio_destroy_buffers(virtio_controller* kvc) {
	if (!kvc) return;

	if (kvc->cmd_buf) {
		avmf_free((uint64_t)kvc->cmd_buf);
		kvc->cmd_buf = NULL;
		memset(kvc->cmd_buf_phys, 0, sizeof(uint64_t) * MAX_CMD_RESP_BUFS);
	}
		
	if (kvc->resp_buf) {
		avmf_free((uint64_t)kvc->resp_buf);
		kvc->resp_buf = NULL;
		memset(kvc->resp_buf_phys, 0, sizeof(uint64_t) * MAX_CMD_RESP_BUFS);
	}
}

static aos_bool virtio_setup_cmd_core(virtio_controller* kvc) {
	kvc->gpu_cmd_core = 0xFFFF;
	smp_get_first_free_core(&kvc->gpu_cmd_core);
	if (kvc->gpu_cmd_core != 0xFFFF) smp_reserve_core(kvc->gpu_cmd_core);
}

static void virtio_release_cmd_core(virtio_controller* kvc) {
	if (!kvc) return;

	if (kvc->gpu_cmd_core != 0xFFFF) {
		enum core_status status = smp_get_core_status(kvc->gpu_cmd_core);
		uint64_t timeout = kget_ms_passed();
		aos_bool invalid_core = AOS_FALSE;
		while (status != CORE_STATUS_RESERVED) {
			switch (status) {
				case CORE_STATUS_READY: {
					// CMD Core Invalid
					invalid_core = AOS_TRUE;
					break;
				}
				default: break;
			}
			if (invalid_core) break;
			if (kget_timestamp_ms() - timeout > 15000) {
				// Drop core, no other choice
				smp_reset_core(kvc->gpu_cmd_core);
			}
		}
		if (!invalid_core) smp_unreserve_core(kvc->gpu_cmd_core);
	}
}

static void virtio_sync_poll(void* kvc_raw) {
	virtio_controller* kvc = (virtio_controller*)kvc_raw;
	uint64_t timeout = kget_timestamp_ms();
    while (kvc->virtq.used->idx != kvc->virtq.avail->idx) {
		if (kget_timestamp_ms() - timeout >= 10000) {
			serial_print("[VirtIO:GPU] Timeout!\n");
			smp_yield();
			return;
		}
        __asm__ volatile("pause");
    }

    uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);

	uint16_t end = kvc->virtq.free_head
	while (end != 0xFFFF) {
		end = kvc->virtq.desc[end].next;
	}

	kvc->virtq.last_used_idx = kvc->virtq.used->idx;
    kvc->worker_buf_slot++;

    spin_unlock_irqrestore(&kvc->virtq_lock, flags);

    serial_print("[VirtIO:GPU] Poll Completed\n");

    smp_yield();
}

static void virtio_submit_async(virtio_controller* kvc, void* cmd, uint64_t cmd_phys, size_t cmd_size, void* resp, uint64_t resp_phys, size_t resp_size) {
    serial_printf("[VirtIO:GPU] Submitting Sync [CMD: %lx, CMD PHYS: %lx, CMD SIZE: %lx]\n", (uint64_t)cmd, cmd_phys, cmd_size);

    uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);

    uint16_t avail_idx = kvc->virtq.avail->idx;
    uint16_t head = kvc->virtq.free_head;
	kvc->virtq.free_head = kvc->virtq.desc[head].next;
    uint16_t next = kvc->virtq.free_head;
	kvc->virtq.free_head = kvc->virtq.desc[next].next;

    // Descriptor 1: The Command (Read-only)
    serial_print("[VirtIO:GPU] Setting Descriptor 1\n");
    kvc->virtq.desc[head].addr = (uintptr_t)cmd_phys;
    kvc->virtq.desc[head].len = cmd_size;
    kvc->virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    kvc->virtq.desc[head].next = next;

    // Descriptor 2: The Response (Write-only for GPU)
    serial_print("[VirtIO:GPU] Setting Descriptor 2\n");
    kvc->virtq.desc[next].addr = (uintptr_t)resp_phys;
    kvc->virtq.desc[next].len = resp_size;
    kvc->virtq.desc[next].flags = VIRTQ_DESC_F_WRITE;
    kvc->virtq.desc[next].next = 0;

    kvc->virtq.avail->ring[avail_idx % kvc->virtq.queue_size] = head;
    __asm__ volatile("mfence" ::: "memory"); // Ensure GPU sees RAM update
    kvc->virtq.avail->idx++;
	__asm__ volatile("mfence" ::: "memory"); // Ensure GPU sees RAM update
    spin_unlock_irqrestore(&kvc->virtq_lock, flags);

    // Notify Doorbell
    serial_print("[VirtIO:GPU] Notifying GPU\n");
    uintptr_t db = kvc->notify_base + (kvc->common_cfg->queue_notify_off * kvc->notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    enum core_status status = 0;
    smp_get_core_status(kvc->gpu_cmd_core, &status);

    if (kvc->gpu_cmd_core != 0xFFFF && status == CORE_STATUS_RESERVED) {
		serial_printf("[VirtIO:GPU] Polling on a Core %u\n", kvc->gpu_cmd_core);
        smp_push_task(kvc->gpu_cmd_core, virtio_sync_poll, kvc); // Push task here
	} else {
		serial_print("[VirtIO:GPU] Polling [Core is not available]\n");
        virtio_sync_poll(kvc); // either no extra core, or core busy
	}

    kvc->main_buf_slot++;
}

static void virtio_submit_sync(virtio_controller* kvc, void* cmd, uint64_t cmd_phys, size_t cmd_size, void* resp, uint64_t resp_phys, size_t resp_size) {
    serial_printf("[VirtIO:GPU] Submitting Sync [CMD: %lx, CMD PHYS: %lx, CMD SIZE: %lx]\n", (uint64_t)cmd, cmd_phys, cmd_size);

    uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);
	uint16_t avail_idx = kvc->virtq.avail->idx;
    uint16_t head = (avail_idx * 2) % kvc->virtq.queue_size;
    uint16_t next = (head + 1) % kvc->virtq.queue_size;

    // Descriptor 1: The Command (Read-only)
    serial_print("[VirtIO:GPU] Setting Descriptor 1\n");
    kvc->virtq.desc[head].addr = (uintptr_t)cmd_phys;
    kvc->virtq.desc[head].len = cmd_size;
    kvc->virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    kvc->virtq.desc[head].next = next;

    // Descriptor 2: The Response (Write-only for GPU)
    serial_print("[VirtIO:GPU] Setting Descriptor 2\n");
    kvc->virtq.desc[next].addr = (uintptr_t)resp_phys;
    kvc->virtq.desc[next].len = resp_size;
    kvc->virtq.desc[next].flags = VIRTQ_DESC_F_WRITE;
    kvc->virtq.desc[next].next = 0;

    kvc->virtq.avail->ring[avail_idx % kvc->virtq.queue_size] = head;
    __asm__ volatile("mfence" ::: "memory"); // Ensure GPU sees RAM update
    kvc->virtq.avail->idx++;
	__asm__ volatile("mfence" ::: "memory"); // Ensure GPU sees RAM update
    spin_unlock_irqrestore(&kvc->virtq_lock, flags);

    // Notify Doorbell
    serial_print("[VirtIO:GPU] Notifying GPU\n");
    uintptr_t db = kvc->notify_base + (kvc->common_cfg->queue_notify_off * kvc->notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    serial_print("[VirtIO:GPU] Polling\n");
    virtio_sync_poll(kvc);
    kvc->main_buf_slot++;
}

static struct virtio_cap get_cap(uint8_t b, uint8_t s, uint8_t f, uint8_t target_type, aos_bool* valid) {
    struct virtio_cap cap = {0};
    uint8_t cap_ptr = pcie_read(b, s, f, 0x34) & 0xFF;
	*valid = AOS_FALSE;

    while (cap_ptr != 0) {
        uint32_t hdr = pcie_read(b, s, f, cap_ptr);
        uint8_t cap_id = hdr & 0xFF;
		uint8_t cap_next = (hdr >> 8) & 0xFF;
		uint8_t cfg_type = (hdr >> 24) & 0xFF;

		uint32_t barinfo = pcie_read(b, s, f, cap_ptr + 4);
		uint8_t bar = barinfo & 0xFF;

		if (bar >= 6) return cap;

        if (cap_id == 0x09) { // Vendor Specific
            if (cfg_type == target_type) {
                cap.cap_ptr = cap_ptr;
                cap.bar = bar;
                cap.offset = pcie_read(b, s, f, cap_ptr + 8);
				cap.length = pcie_read(b, s, f, cap_ptr + 12);
				*valid = AOS_TRUE;
                return cap;
            }
        }
        cap_ptr = cap_next;
    }
    return cap;
}

static aos_bool virtio_map(virtio_controller* kvc) {
	if (!kvc) return AOS_FALSE;

	uint8_t bus = kvc->gpu->pcie_device->bus;
    uint8_t slot = kvc->gpu->pcie_device->slot;
    uint8_t func = kvc->gpu->pcie_device->func;

	aos_bool valid = AOS_FALSE;
	struct virtio_cap common_cap = get_cap(bus, slot, func, 1, &valid);
	if (!valid) {
		serial_print("[VirtIO:GPU] Invailid CAP\n");
		return AOS_FALSE;
	}

    uint32_t bar = pcie_read_bar(bus, slot, func, common_cap.bar);
    uint64_t bar_phys = bar & ~0xFULL;
	if ((bar_phys & 0xFFF) != 0) { // BAR should be page-aligned
		serial_print("[VirtIO:GPU] BAR0 is not page-aligned, mapping failed!\n");
		return AOS_FALSE;
	}

    aos_bool is_64bit = ((bar >> 1) & 0b011) == 0x2;
	uint32_t orig0 = bar;
    uint32_t orig1 = 0;

	if ((bar & 0b001) == 0x1) {
		serial_print("[VirtIO:GPU] BAR0 is not a memory BAR, mapping failed!\n");
		return AOS_FALSE;
	}

    if (is_64bit) {
        orig1 = pcie_read_bar(bus, slot, func, common_cap.bar+1);
        bar_phys |= ((uint64_t)orig1 << 32);
    }

	aos_bool memspace = pcie_get_memory_space_toggled(bus, slot, func);
	if (memspace) pcie_toggle_memory_space(bus, slot, func, AOS_FALSE);

    pcie_write_bar(bus, slot, func, common_cap.bar, 0xFFFFFFFF);
    if (is_64bit) pcie_write_bar(bus, slot, func, common_cap.bar+1, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read_bar(bus, slot, func, common_cap.bar);
    uint32_t mask1 = is_64bit ? pcie_read_bar(bus, slot, func, common_cap.bar+1) : 0;

    pcie_write_bar(bus, slot, func, common_cap.bar, orig0);
    if (is_64bit) pcie_write_bar(bus, slot, func, common_cap.bar+1, orig1);

	if (memspace) pcie_toggle_memory_space(bus, slot, func, AOS_TRUE);

	uint64_t size = 0;
	if (is_64bit) {
		uint64_t mask = (mask0 & ~0xFULL) | ((uint64_t)mask1 << 32);
		size = ~mask + 1;
	} else {
		uint32_t mask = (uint32_t)(mask0 & ~0xFULL);
		size = (uint32_t)(~mask + 1);
	}

    kvc->mapping_size = size;
	if (kvc->mapping_size < sizeof(struct virtio_common_cfg)) {
		serial_print("[VirtIO:GPU] Device reported memory size is lower than minimum, mapping failed!\n");
		return AOS_FALSE;
	}

	pager_map_range(AOS_DIRECT_MAP_BASE + bar_phys, bar_phys, kvc->mapping_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	pcie_toggle_busmaster(bus, slot, func, AOS_TRUE);

	kvc->common_cfg = (volatile struct virtio_common_cfg*)(AOS_DIRECT_MAP_BASE + bar_phys + common_cap.offset);
	serial_printf("[VirtIO:GPU] Mapped all VIRTIO COMMON CFG (Size: 0x%llx)\n", kvc->mapping_size);

	return AOS_TRUE;
}

static void virtio_destroy(virtio_controller* kvc) {
	if (kvc->common_cfg) {
		kvc->common_cfg->device_status |= VIRTIO_STATUS_FAILED;
		__asm__ volatile("mfence" ::: "memory");
	}

	virtio_destroy_buffers(kvc);
	virtio_destroy_queues(kvc);
	virtio_release_cmd_core(kvc);

	kvc->valid = AOS_FALSE;
	if (kvc->idx == controller_count-1) controller_count--;
}

aos_bool virtio_flush(struct gpu_device* gpu_, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id) {
    if (gpu_->controller_idx >= controller_count || !controllers) return AOS_FALSE;
	virtio_controller* kvc = &controllers[gpu_->controller_idx];
	struct gpu_device* gpu = kvc->gpu; // ensure we use the linked gpu
	
	while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)&kvc->cmd_buf[cur_buf_slot];
    memset(f, 0, sizeof(*f));
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->r.x = x;
    f->r.y = y;
    f->r.width = w;
    f->r.height = h;
    f->resource_id = resource_id;
    f->padding = 0;

    virtio_submit_async(kvc, f, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*f), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
	return AOS_TRUE;
}

// Total hours wasted on virtio: 18 (I DID COOK)
aos_bool virtio_init(struct AOS_Module* m) {
	if (m->hdr.type != MODULE_TYPE_DRIVER) return AOS_FALSE;
    if (m->Modules.driver_module.type != MODULE_DRIVER_TYPE_GPU) return AOS_FALSE;

	if (!controllers) {
		controllers = (virtio_controller*)avmf_alloc(sizeof(virtio_controller) * KVIRTIO_ALLOC_STEP, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!controllers) return AOS_FALSE;
		controller_cap = KVIRTIO_ALLOC_STEP;
		controller_count = 0;
	} else if (controller_count >= controller_cap) {
		virtio_controller* nptr = (virtio_controller*)avmf_alloc(sizeof(virtio_controller) * (controller_cap + KVIRTIO_ALLOC_STEP), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!nptr) return AOS_FALSE;
		memcpy(nptr, controllers, sizeof(virtio_controller)*controller_count);
		avmf_free((uint64_t)controllers);
		controllers = nptr;
		controller_cap += KVIRTIO_ALLOC_STEP;
	}

	struct gpu_device* gpu = &m->Modules.driver_module.DriverConnections.gpu_connector;
	virtio_controller* kvc = &controllers[controller_count];
	memset(kvc, 0, sizeof(virtio_controller));
	kvc->idx = controller_count;
	kvc->gpu = gpu;
	kvc->valid = AOS_FALSE;
	gpu->controller_idx = controller_count;
	controller_count++;

    pcie_device_t* dev = &m->Modules.driver_module.pcie_device;
    PCIe_FB* fb = gpu->framebuffer;

    serial_print("[VIRTIO DRIVER] Initializing...\n");
	if (!virtio_map(kvc)) {
		virtio_destroy(kvc);
		return AOS_FALSE;
	}
    
	aos_bool valid_cap = AOS_FALSE;
    struct virtio_cap notify_cap = get_cap(dev->bus, dev->slot, dev->func, 2, &valid_cap);
	if (!valid_cap) {
		serial_print("[VirtIO:GPU] Invailid CAP\n");
		virtio_destroy(kvc);
		return AOS_FALSE;
	}
    uint32_t n_bar_val = pcie_read_bar(dev->bus, dev->slot, dev->func, notify_cap.bar);
    kvc->notify_base = (n_bar_val & ~0xF) + notify_cap.offset + AOS_DIRECT_MAP_BASE;
    kvc->notify_multiplier = pcie_read(dev->bus, dev->slot, dev->func, notify_cap.cap_ptr + 16);

    // RESET
    if (!virtio_reset_device(kvc)) {
		virtio_destroy(kvc);
		return AOS_FALSE;
	}

    // Features
    if (!virtio_negotiate_features(kvc)) {
		virtio_destroy(kvc);
		return AOS_FALSE;
	}

    // Queues
    if (!virtio_setup_queues(kvc, 0)) {
		virtio_destroy(kvc);
		return AOS_FALSE;
	}
    
    // setup buffers
	if (!virtio_setup_buffers(kvc)) {
		virtio_destroy(kvc);
		return AOS_FALSE;
	}

    kvc->common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
	__asm__ volatile("mfence" ::: "memory");
	kdelay_ns(300);

	uint8_t final_status = kvc->common_cfg->device_status;
	if ((final_status & VIRTIO_STATUS_FAILED) || !(final_status & VIRTIO_STATUS_DRIVER_OK)) {
        serial_printf("[VirtIO:GPU] Device rejected DRIVER_OK initialization status! (Status: 0x%x)\n", final_status);
        virtio_destroy(kvc);
        return AOS_FALSE;
    }

    // Setup gpu core
	if (!virtio_setup_cmd_core(kvc)) {
		kvc->gpu_cmd_core = 0xFFFF;
	}

    // Get fb info
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_ctrl_hdr* get_info = (struct virtio_gpu_ctrl_hdr*)&kvc->cmd_buf[cur_buf_slot];
    memset(get_info, 0, sizeof(*get_info));
    get_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    virtio_submit_sync(kvc, get_info, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*get_info), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_resp_display_info));
    struct virtio_gpu_resp_display_info* display = (struct virtio_gpu_resp_display_info*)&kvc->resp_buf[cur_buf_slot];

    if (display->displays[0].enabled) {
        gpu->framebuffer->w = display->displays[0].r.width;
        gpu->framebuffer->h = display->displays[0].r.height;
    } else {
        // Fallback if the device doesn't have a preference
        gpu->framebuffer->w = 1024;
        gpu->framebuffer->h = 768;
    }
    gpu->framebuffer->bpp = 32;
    gpu->framebuffer->pitch = gpu->framebuffer->w * (gpu->framebuffer->bpp / 8);
    gpu->framebuffer->size = gpu->framebuffer->pitch * gpu->framebuffer->h;

    gpu->active = AOS_TRUE;
    serial_print("[VirtIO:GPU] Initialization completed!\n");
	kvc->valid = AOS_TRUE;
	return AOS_TRUE;
}

aos_bool virtio_init_resources(struct gpu_device* gpu_, int id) {
	if (gpu_->controller_idx >= controller_count || !controllers) return AOS_FALSE;
	virtio_controller* kvc = &controllers[gpu_->controller_idx];
	struct gpu_device* gpu = kvc->gpu; // ensure we use the linked gpu

    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uintptr_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing Resources...\n");

    // make resources
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_create_2d* create = (struct virtio_gpu_resource_create_2d*)&kvc->cmd_buf[cur_buf_slot];
    memset(create, 0, sizeof(*create));
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = id;
    create->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    create->width = fb->w;
    create->height = fb->h;
    virtio_submit_async(kvc, create, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*create), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_attach_backing* attach = (struct virtio_gpu_resource_attach_backing*)&kvc->cmd_buf[cur_buf_slot];
    memset(attach, 0, sizeof(*attach));
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = id;
    attach->nr_entries = 1;
    struct virtio_gpu_mem_entry* entry = (struct virtio_gpu_mem_entry*)((uintptr_t)attach + sizeof(*attach));
    entry->addr = fb->phys;
    entry->length = fb->size;
    virtio_submit_async(kvc, attach, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*attach) + sizeof(*entry), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_set_scanout* scanout = (struct virtio_gpu_set_scanout*)&kvc->cmd_buf[cur_buf_slot];
    memset(scanout, 0, sizeof(*scanout));
    scanout->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout->scanout_id = 0;
    scanout->resource_id = id;
    scanout->r.x = 0;
    scanout->r.y = 0;
    scanout->r.width = fb->w;
    scanout->r.height = fb->h;
    virtio_submit_async(kvc, scanout, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*scanout), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    serial_print("[VirtIO:GPU] Initialization of resources completed!\n");
	return AOS_TRUE;
}

aos_bool virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    (void)gpu; (void)w; (void)h; (void)bpp;
	return AOS_TRUE;
}

aos_bool virtio_switch_off(struct gpu_device* gpu_) {
	if (gpu_->controller_idx >= controller_count || !controllers) return AOS_FALSE;
	virtio_controller* kvc = &controllers[gpu_->controller_idx];
	struct gpu_device* gpu = kvc->gpu; // ensure we use the linked gpu
	kvc->valid = AOS_FALSE;
	gpu->active = AOS_FALSE;

    serial_print("[VirtIO:GPU] Switching Off...\n");
    if (!virtio_reset_device(kvc)) {
		kvc->valid = AOS_TRUE;
		gpu->active = AOS_TRUE;
		return AOS_FALSE;
	}
	serial_print("[VirtIO:GPU] GPU Reset completed!\n[VirtIO:GPU] Unmapping used memory!\n");

	virtio_destroy_buffers(kvc);
	virtio_destroy_queues(kvc);
	virtio_release_cmd_core(kvc);
    serial_print("[VirtIO:GPU] Unmapping completed!\n");

    serial_print("[VirtIO:GPU] Switched off!\n");
	return AOS_TRUE;
}

aos_bool virtio_refresh(struct gpu_device* gpu_, uint64_t flags) {
	if (gpu_->controller_idx >= controller_count || !controllers) return AOS_FALSE;
	virtio_controller* kvc = &controllers[gpu_->controller_idx];
	struct gpu_device* gpu = kvc->gpu; // ensure we use the linked gpu

	// Start of by refreshing gpu core
	if (flags & GPU_REFRESH_FLAG_CORE) {
		virtio_release_cmd_core(kvc);
		virtio_setup_cmd_core(kvc);
	}

	// Now refresh device
	if (flags & GPU_REFRESH_FLAG_BUFFERS) {
		uint64_t phys = 0;
		struct virtio_gpu_ctrl_hdr* buf = (struct virtio_gpu_ctrl_hdr*)avmf_alloc(MAX_CMD_RESP_BUFS * sizeof(struct virtio_gpu_ctrl_hdr), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &phys);
		if (buf && phys) {
			avmf_free((uint64_t)kvc->cmd_buf);
			kvc->cmd_buf = buf;
			for (uint8_t i = 0; i < MAX_CMD_RESP_BUFS; i++) {
				kvc->cmd_buf_phys[i] = phys + (sizeof(struct virtio_gpu_ctrl_hdr) * i);
			}
		}

		phys = 0;
		struct virtio_gpu_resp_display_info* buf = (struct virtio_gpu_resp_display_info*)avmf_alloc(MAX_CMD_RESP_BUFS * sizeof(struct virtio_gpu_ctrl_hdr), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &phys);
		if (buf && phys) {
			avmf_free((uint64_t)kvc->resp_buf);
			kvc->resp_buf = buf;
			for (uint8_t i = 0; i < MAX_CMD_RESP_BUFS; i++) {
				kvc->resp_buf_phys[i] = phys + (sizeof(struct virtio_gpu_resp_display_info) * i);
			}
		}
	}

	if ((flags & GPU_REFRESH_FLAG_QUEUES) || (flags & GPU_REFRESH_FLAG_DEVICE)) {
		// Reset everything since we cannot reset only queues as they are live
		while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
			__asm__ volatile("pause"); 
		}

		kvc->valid = AOS_FALSE;
		kvc->gpu->active = AOS_FALSE;
		if (!virtio_reset_device(kvc)) {
			serial_print("[VirtIO:GPU] Refresh Failed!\n");
			kvc->valid = AOS_TRUE;
			kvc->gpu->active = AOS_TRUE;
		}

		// Just unmap memory here since everything after this is unrevertable
		virtio_destroy_queues(kvc);

		if (!virtio_negotiate_features(kvc)) {
			serial_print("[VirtIO:GPU] Refresh Failed! [Cannot Revert changes!]\n");
			virtio_destroy(kvc);
			return AOS_FALSE;
		}
		if (!virtio_setup_queues(kvc, 0)) {
			serial_print("[VirtIO:GPU] Refresh Failed! [Cannot Revert changes!]\n");
			virtio_destroy(kvc);
			return AOS_FALSE;
		}
		
		kvc->common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
		__asm__ volatile("mfence" ::: "memory");
		kdelay_ns(300);

		uint8_t final_status = kvc->common_cfg->device_status;
		if ((final_status & VIRTIO_STATUS_FAILED) || !(final_status & VIRTIO_STATUS_DRIVER_OK)) {
			serial_print("[VirtIO:GPU] Refresh Failed! [Cannot Revert Changes]\n", final_status);
			virtio_destroy(kvc);
			return AOS_FALSE;
		}

		// Get fb info
		while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
			__asm__ volatile("pause"); 
		}
		uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

		struct virtio_gpu_ctrl_hdr* get_info = (struct virtio_gpu_ctrl_hdr*)&kvc->cmd_buf[cur_buf_slot];
		memset(get_info, 0, sizeof(*get_info));
		get_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
		virtio_submit_sync(kvc, get_info, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*get_info), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_resp_display_info));
		struct virtio_gpu_resp_display_info* display = (struct virtio_gpu_resp_display_info*)&kvc->resp_buf[cur_buf_slot];

		if (display->displays[0].enabled) {
			gpu->framebuffer->w = display->displays[0].r.width;
			gpu->framebuffer->h = display->displays[0].r.height;
		} else {
			// Fallback if the device doesn't have a preference
			gpu->framebuffer->w = 1024;
			gpu->framebuffer->h = 768;
		}
		gpu->framebuffer->bpp = 32;
		gpu->framebuffer->pitch = gpu->framebuffer->w * (gpu->framebuffer->bpp / 8);
		gpu->framebuffer->size = gpu->framebuffer->pitch * gpu->framebuffer->h;

		gpu->active = AOS_TRUE;
		kvc->valid = AOS_TRUE;
	}

	serial_print("[VirtIO:GPU] Refresh Completed!\n");
}

static void virtio_create_context(virtio_controller* kvc, uint32_t ctx_id) {
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;
    struct virtio_gpu_ctx_create* cmd = (struct virtio_gpu_ctx_create*)&kvc->cmd_buf[cur_buf_slot];
    memset(cmd, 0, sizeof(*cmd));
    
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
    cmd->hdr.ctx_id = ctx_id;
    cmd->nlen = 14;
    memcpy(cmd->debug_name, "Pyrion-Context", 14);
    
    virtio_submit_sync(kvc, cmd, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*cmd), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

static void virtio_destroy_context(virtio_controller* kvc, uint32_t ctx_id) {
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;
    struct virtio_gpu_ctx_destroy* cmd = (struct virtio_gpu_ctx_destroy*)&kvc->cmd_buf[cur_buf_slot];
    memset(cmd, 0, sizeof(*cmd));
    
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
    cmd->hdr.ctx_id = ctx_id;
    
    virtio_submit_sync(kvc, cmd, kvc->cmd_buf_phys[cur_buf_slot], sizeof(*cmd), &kvc->resp_buf[cur_buf_slot], kvc->resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

// Pyrion implementation
#include <inc/drivers/gpu/apis/pyrion.h>
#define MAX_PYRION_CONTEXTS 256

static struct pyrion_ctx* p_contexts[MAX_PYRION_CONTEXTS];
static uint64_t p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
static uint32_t current_font_resource_id = 999;

void pyrion_init_virtio(void) {
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        uint64_t ctx_phys = 0;
        uint64_t ctx_virt = (uint64_t)avmf_alloc(sizeof(struct pyrion_ctx), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &ctx_phys);
        if (!ctx_phys || !ctx_virt) continue;

        struct pyrion_ctx* ctx = (struct pyrion_ctx*)ctx_virt;
        ctx->ctx_phys = ctx_phys;
        ctx->ctx_id = i + 1;
        ctx->driver_var = 0;
        ctx->res_id = 0;
        ctx->valid = 0; // NOT VALID
        p_contexts[i] = ctx;
        if (p_nxt_ctx > MAX_PYRION_CONTEXTS) // Not set yet
            p_nxt_ctx = i;
    }
}

void pyrion_finish_virtio(void) {
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        struct pyrion_ctx* ctx = (struct pyrion_ctx*)p_contexts[i];
		if (ctx->controller_idx > controller_count || !controllers) continue;
		virtio_controller* kvc = &controllers[ctx->controller_idx];

        if (!ctx || !ctx->ctx_phys) continue;

        if (ctx->valid == 1) {
            virtio_destroy_context(kvc, i);
            if (ctx->driver_data) avmf_free((uint64_t)ctx->driver_data);
        }

        avmf_free((uint64_t)ctx);
        p_contexts[i] = NULL;
    }
    p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
}

static uint64_t pyrion_get_free_ctx_slot(void) {
    if (p_nxt_ctx < MAX_PYRION_CONTEXTS) {
        uint64_t id = p_nxt_ctx;
        p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
        return id;
    }
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        if (p_contexts[i] && p_contexts[i]->valid == 0)
            return i;
    }
    return 0xFFFFFFFFFFFFFFFF;
}

static void pyrion_push_virgl(struct pyrion_ctx* ctx, uint32_t opcode, uint32_t obj_type, uint32_t* args, uint32_t arg_count) {
    if (ctx == NULL || ctx->driver_data == NULL) return;
    uint32_t current_size = ctx->driver_var;

    uint32_t* write_ptr = (uint32_t*)((uintptr_t)ctx->driver_data + current_size);
    *write_ptr = VIRGL_CMD_HEADER(opcode, obj_type, arg_count & 0xFF);
    write_ptr++;

    for (uint32_t i = 0; i < arg_count; i++) {
        *write_ptr = args[i];
        write_ptr++;
    }
    
    ctx->driver_var += (1 + arg_count) * sizeof(uint32_t);
}

static uint32_t rgba_to_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | ((uint32_t)a);
}

struct pyrion_ctx* pyrion_create_ctx_virtio(void) {
	if (0 >= controller_count || !controllers) return NULL;
	virtio_controller* kvc = &controllers[0];

    serial_print("[Pyrion] Creating Context...\n");

    uint64_t slot = pyrion_get_free_ctx_slot();
    if (slot > MAX_PYRION_CONTEXTS) {
        serial_print("[Pyrion] Context Limit Reached!\n");
        return NULL;
    }

    struct pyrion_ctx* ctx = (struct pyrion_ctx*)p_contexts[slot];
    ctx->driver_data = (void*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &ctx->driver_data_phys);
    if (!ctx->driver_data) return NULL;
    memset(ctx->driver_data, 0, 0x1000);

    virtio_create_context(kvc, ctx->ctx_id);
    
    ctx->viewport.x = 0; ctx->viewport.y=0; ctx->viewport.width=0; ctx->viewport.height; ctx->viewport.color = 0;
    ctx->valid = 1;

    serial_print("[Pyrion] Context Created\n");
    return ctx;
}

void pyrion_destroy_ctx_virtio(struct pyrion_ctx* ctx) {
    if (ctx == NULL || ctx->ctx_id > 0) return;
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    virtio_destroy_context(kvc, ctx->ctx_id);

    if (ctx->driver_data) avmf_free((uint64_t)ctx->driver_data);

    p_nxt_ctx = ctx->ctx_id;
    ctx->valid = 0;
}

void pyrion_viewport_virtio(struct pyrion_ctx* ctx, struct pyrion_rect* viewport) {
    if (ctx == NULL || ctx->ctx_id > MAX_PYRION_CONTEXTS) return;
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    uint64_t res_id = ctx->ctx_id + MAX_PYRION_CONTEXTS; // Ensure not a single resource id causes trouble

    // Create a 3D Resource (Texture) on the Host GPU
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;
    
    struct virtio_gpu_resource_create_3d* c3d = (struct virtio_gpu_resource_create_3d*)&kvc->cmd_buf[slot];
    memset(c3d, 0, sizeof(*c3d));
    c3d->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    c3d->resource_id = res_id;
    c3d->target = VIRTIO_GPU_PIPE_TEXTURE_2D;
    c3d->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    c3d->bind = VIRTIO_GPU_BIND_RENDER_TARGET | VIRTIO_GPU_BIND_SCANOUT;
    c3d->width = viewport->width;
    c3d->height = viewport->height;
    c3d->depth = 1;
    c3d->last_level = 0;
    c3d->nr_samples = 0;
    c3d->flags = VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP;

    virtio_submit_sync(kvc, c3d, kvc->cmd_buf_phys[slot], sizeof(*c3d), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Attach backing
    ctx->driver_data2 = (void*)avmf_alloc(viewport->width * viewport->height * 4, MALLOC_TYPE_SENSITIVE, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &ctx->driver_data_phys2);
    if (!ctx->driver_data2) {
        serial_print("[VirtIO:GPU] Failed to allocate for backing!\n");
        return;
    }

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct attb {
        struct virtio_gpu_resource_attach_backing att;
        struct virtio_gpu_mem_entry entry;
    } __attribute__((packed));

    struct attb* attb = (struct attb*)&kvc->cmd_buf[slot];
    memset(attb, 0, sizeof(*attb));
    attb->att.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attb->att.resource_id = res_id;
    attb->att.nr_entries = 1;
    attb->entry.addr = (uint64_t)ctx->driver_data_phys2;
    attb->entry.length = viewport->width * viewport->height * 4;

    virtio_submit_sync(kvc, attb, kvc->cmd_buf_phys[slot], sizeof(*attb), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Attach the resource to the 3D Context
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_ctx_resource* att = (struct virtio_gpu_ctx_resource*)&kvc->cmd_buf[slot];
    memset(att, 0, sizeof(*att));
    att->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    att->hdr.ctx_id = ctx->ctx_id;
    att->resource_id = res_id;
    
    virtio_submit_sync(kvc, att, kvc->cmd_buf_phys[slot], sizeof(*att), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Set scanout
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_set_scanout* scanout = (struct virtio_gpu_set_scanout*)&kvc->cmd_buf[slot];
    memset(scanout, 0, sizeof(*scanout));
    scanout->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout->resource_id = res_id;
    scanout->scanout_id = 0; // The first monitor
    scanout->r.width = viewport->width;
    scanout->r.height = viewport->height;
    scanout->r.x = 0;
    scanout->r.y = 0;

    virtio_submit_sync(kvc, scanout, kvc->cmd_buf_phys[slot], sizeof(*scanout), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    ctx->res_id = res_id;
    ctx->viewport = *viewport;

    if (kvc->acceleration_present != 1) {
        serial_print("[VirtIO:GPU] No Acceleration in Viewport/Context!\n");
        FB_Info_t* fb = (FB_Info_t*)avmf_alloc(viewport->width * viewport->height * sizeof(uint32_t), MALLOC_TYPE_SENSITIVE, PAGE_RW | PAGE_PRESENT | PAGE_PCD, &ctx->fb.phys_addr);
        if (!fb) {
            serial_print("[VirtIO:GPU] Failed to allocate framebuffer\n");
            return;
        }
        ctx->fb.addr = (uint64_t)fb;
        ctx->fb.width = viewport->width;
        ctx->fb.height = viewport->height;
        ctx->fb.bpp = sizeof(uint32_t) * 8;
        ctx->fb.pitch = viewport->width * sizeof(uint32_t);
        return;
    }

    uint32_t surf_args[6] = {
        ctx->res_id + MAX_PYRION_CONTEXTS,
        ctx->res_id,
        VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
        VIRTIO_GPU_BIND_RENDER_TARGET,
        viewport->width,
        viewport->height
    };
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CREATE_OBJECT, VIRTIO_VIRGL_OBJECT_SURFACE, surf_args, 6);
    uint32_t fb_args[3] = {
        1,
        0,
        ctx->res_id + MAX_PYRION_CONTEXTS
    };
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_FRAMEBUFFER_STATE, VIRTIO_VIRGL_OBJECT_NULL, fb_args, 3);

    float width = (float)viewport->width;
    float height = (float)viewport->height;

    float view_args[6];
    ((float*)view_args)[0] = width / 2.0f; // Scale X
    ((float*)view_args)[1] = height / 2.0f; // Scale Y
    ((float*)view_args)[2] = 0.5f; // Scale Z (Depth)
    ((float*)view_args)[3] = width / 2.0f; // Translate X
    ((float*)view_args)[4] = height / 2.0f; // Translate Y
    ((float*)view_args)[5] = 0.5f; // Translate Z
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_VIEWPORT_STATE, VIRTIO_VIRGL_OBJECT_NULL, (uint32_t*)view_args, 6);

    uint32_t init_scissor[2] = {0, (viewport->height << 16) | viewport->width};
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, init_scissor, 2);

    uint32_t rast_handle = ctx->res_id + (MAX_PYRION_CONTEXTS * 2);
    uint32_t rast_args[9] = {0};
    rast_args[0] = rast_handle;
    rast_args[1] = 0x00000001;
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CREATE_OBJECT, VIRTIO_VIRGL_OBJECT_RASTERIZER, rast_args, 9);
    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_BIND_OBJECT, VIRTIO_VIRGL_OBJECT_RASTERIZER, &rast_handle, 1);
    
    pyrion_flush_virtio(ctx);
}

void pyrion_flush_virtio(struct pyrion_ctx* ctx) {
    if (ctx == NULL || ctx->valid != 1) return;
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    if (kvc->acceleration_present != 1) {
        virtio_flush(kvc->gpu, ctx->viewport.x, ctx->viewport.y, ctx->viewport.width, ctx->viewport.height, ctx->res_id);
        return;
    }

    uint32_t stream_size = ctx->driver_var;
    if (stream_size == 0) return;
    if (!ctx->driver_data || stream_size < 1) {
        while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
            __asm__ volatile("pause"); 
        }
        uint64_t slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

        struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)&kvc->cmd_buf[slot];
        memset(f, 0, sizeof(*f));
        f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
        f->hdr.ctx_id = ctx->ctx_id;
        f->r.x = ctx->viewport.x;
        f->r.y = ctx->viewport.y;
        f->r.width = ctx->viewport.width;
        f->r.height = ctx->viewport.height;
        f->resource_id = ctx->res_id;
        f->padding = 0;

        virtio_submit_async(kvc, f, kvc->cmd_buf_phys[slot], sizeof(*f), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));
        return;
    }

    for (uintptr_t i = (uintptr_t)ctx->driver_data; i < (uintptr_t)ctx->driver_data + stream_size; i += 64) { // 64 is the standard x86 cache line size
        __asm__ volatile("clflush (%0)" : : "r"(i) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_cmd_submit_3d* virgl = (struct virtio_gpu_cmd_submit_3d*)&kvc->cmd_buf[slot];
    virgl->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
    virgl->hdr.ctx_id = ctx->ctx_id;
    virgl->size = stream_size;

    uint16_t head = kvc->virtq.free_head;
    uint16_t next1 = (head + 1) % kvc->virtq.queue_size;
    uint16_t next2 = (next1 + 1) % kvc->virtq.queue_size;

    uint64_t flags = spin_lock_irqsave(&kvc->virtq_lock);
    // Descriptor 1: The Command (Read-only)
    kvc->virtq.desc[head].addr = (uintptr_t)kvc->cmd_buf_phys[slot];
    kvc->virtq.desc[head].len = sizeof(*virgl);
    kvc->virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    kvc->virtq.desc[head].next = next1;

    // Descriptor 2: The stream (Ready-Only)
    kvc->virtq.desc[next1].addr = (uintptr_t)(ctx->driver_data_phys);
    kvc->virtq.desc[next1].len = stream_size;
    kvc->virtq.desc[next1].flags = VIRTQ_DESC_F_NEXT;
    kvc->virtq.desc[next1].next = next2;

    // Descriptor 3: The response (Write-Only)
    kvc->virtq.desc[next2].addr = kvc->resp_buf_phys[slot];
    kvc->virtq.desc[next2].len = sizeof(struct virtio_gpu_ctrl_hdr);
    kvc->virtq.desc[next2].flags = VIRTQ_DESC_F_WRITE;
    kvc->virtq.desc[next2].next = 0;

    kvc->virtq.avail->ring[kvc->virtq.avail->idx % kvc->virtq.queue_size] = head;
    __asm__ volatile("mfence" ::: "memory");
    kvc->virtq.avail->idx++;

    kvc->virtq.free_head = (next2 + 1) % kvc->virtq.queue_size;
    spin_unlock_irqrestore(&kvc->virtq_lock, flags);

    // Notify Doorbell
    uintptr_t db = kvc->notify_base + (kvc->common_cfg->queue_notify_off * kvc->notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    while (kvc->virtq.used->idx != kvc->virtq.avail->idx) {
        __asm__ volatile("pause");
    }
    
    ctx->driver_var = 0;

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;
    struct virtio_gpu_transfer_to_host_3d* t = (struct virtio_gpu_transfer_to_host_3d*)&kvc->cmd_buf[slot];
    memset(t, 0, sizeof(*t));
    t->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D;
    t->hdr.ctx_id = ctx->ctx_id;
    t->resource_id = ctx->res_id;
    t->w = ctx->viewport.width;
    t->h = ctx->viewport.height;
    t->d = 1;

    virtio_submit_sync(kvc, t, kvc->cmd_buf_phys[slot], sizeof(*t), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));
    
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)&kvc->cmd_buf[slot];
    memset(f, 0, sizeof(*f));
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->hdr.ctx_id = ctx->ctx_id;
    f->r.x = ctx->viewport.x;
    f->r.y = ctx->viewport.y;
    f->r.width = ctx->viewport.width;
    f->r.height = ctx->viewport.height;
    f->resource_id = ctx->res_id;
    f->padding = 0;

    virtio_submit_async(kvc, f, kvc->cmd_buf_phys[slot], sizeof(*f), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

void pyrion_clear_virtio(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    if (kvc->acceleration_present != 1) {
        if (!ctx->fb.addr) return;
        fb_clear(&ctx->fb, rgba_to_u32(r, g, b, a));
        return;
    }

    float color[4] = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    uint32_t args[5];
    args[0] = 0x7;
    memcpy(&args[1], color, sizeof(color));

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);
}

void pyrion_pixel_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    if (kvc->acceleration_present != 1) {
        if (!ctx->fb.addr) return;
        fb_put_pixel(&ctx->fb, x, y, rgba_to_u32(r, g, b, a));
        return;
    }
    uint32_t scissor_args[2];
    scissor_args[0] = (y << 16) | x;
    scissor_args[1] = ((y + 1) << 16) | (x + 1);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
    
    float color[4] = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    uint32_t args[5];
    args[0] = 0x7;
    memcpy(&args[1], color, sizeof(color));

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);

    scissor_args[0] = (ctx->viewport.y << 16) | ctx->viewport.x;
    scissor_args[1] = ((ctx->viewport.y + ctx->viewport.height) << 16) | (ctx->viewport.x + ctx->viewport.width);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
}

void pyrion_draw_char_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t atlas_x, uint32_t atlas_y, uint32_t w, uint32_t h, uint32_t font_res_id) {
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    if (kvc->acceleration_present != 1) {
        if (!ctx->fb.addr) return;
        fb_printc(&ctx->fb, &ctx->fb_info, '-');
        return;
    }
    uint32_t args[13];
    args[0] = ctx->res_id; // Destination (The Window)
    args[1] = 0; // Dest level
    args[2] = x; // Dest X
    args[3] = y; // Dest Y
    args[4] = 0; // Dest Z
    args[5] = font_res_id; // Source (The Font Atlas)
    args[6] = 0; // Source level
    args[7] = atlas_x; // Source X (Where char is in atlas)
    args[8] = atlas_y; // Source Y
    args[9] = 0; // Source Z
    args[10] = w; // Width of char
    args[11] = h; // Height of char
    args[12] = 1; // Depth

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_RESOURCE_COPY_REGION, VIRTIO_VIRGL_OBJECT_NULL, args, 13);
}

void pyrion_destroy_font_virtio(struct pyrion_ctx* ctx, uint32_t font_res_id, void* font_mem) {
	if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_unref* unref = (struct virtio_gpu_resource_unref*)&kvc->cmd_buf[slot];
    memset(unref, 0, sizeof(*unref));
    unref->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    unref->resource_id = font_res_id;

    virtio_submit_sync(kvc, unref, kvc->cmd_buf_phys[slot], sizeof(*unref), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    if (font_mem) {
        avmf_free((uint64_t)font_mem);
    }
}

uint32_t pyrion_upload_font_virtio(struct pyrion_ctx* ctx, uint64_t atlas_phys, uint32_t* atlas, uint32_t atlas_w, uint32_t atlas_total_h) {
	if (ctx->controller_idx >= controller_count || !controllers) return 0;
	virtio_controller* kvc = &controllers[ctx->controller_idx];

    uint32_t res_id = current_font_resource_id++;

    // Create 3D Resource
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_create_3d* c3d = (struct virtio_gpu_resource_create_3d*)&kvc->cmd_buf[slot];
    memset(c3d, 0, sizeof(*c3d));
    c3d->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    c3d->hdr.ctx_id = 0;
    c3d->resource_id = res_id;
    c3d->target = VIRTIO_GPU_PIPE_TEXTURE_2D;
    c3d->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    c3d->bind = VIRTIO_GPU_BIND_SAMPLER_VIEW;
    c3d->width = atlas_w;
    c3d->height = atlas_total_h;
    c3d->depth = 1;
    c3d->array_size = 1;
    virtio_submit_sync(kvc, c3d, kvc->cmd_buf_phys[slot], sizeof(*c3d), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    struct virtio_gpu_ctrl_hdr* resp = (struct virtio_gpu_ctrl_hdr*)&kvc->resp_buf[slot];
    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_printf("[VIRTIO:PYRION] Font upload failed (during create). resp type: %x\n", resp->type);
        return 0;
    }

    // Attach Backing
    struct cmd_struct {
        struct virtio_gpu_resource_attach_backing att;
        struct virtio_gpu_mem_entry entry;
    } __attribute__((packed));
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct cmd_struct* cmd = (struct cmd_struct*)&kvc->cmd_buf[slot];
    memset(cmd, 0, sizeof(*cmd));
    cmd->att.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->att.hdr.ctx_id = 0;
    cmd->att.resource_id = res_id;
    cmd->att.nr_entries = 1;
    cmd->entry.addr = atlas_phys;
    cmd->entry.length = atlas_w * atlas_total_h * sizeof(uint32_t);
    virtio_submit_sync(kvc, cmd, kvc->cmd_buf_phys[slot], sizeof(*cmd), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    resp = (struct virtio_gpu_ctrl_hdr*)&kvc->resp_buf[slot];
    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_printf("[VIRTIO:PYRION] Font upload failed (during attach_backing). resp type: %x\n", resp->type);
        return 0;
    }

    // Attach to ctx
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_ctx_resource* link = (struct virtio_gpu_ctx_resource*)&kvc->cmd_buf[slot];
    memset(link, 0, sizeof(*link));
    link->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    link->hdr.ctx_id = ctx->ctx_id;
    link->resource_id = res_id;
    virtio_submit_sync(kvc, link, kvc->cmd_buf_phys[slot], sizeof(*link), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    resp = (struct virtio_gpu_ctrl_hdr*)&kvc->resp_buf[slot];
    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_printf("[VIRTIO:PYRION] Font upload failed (during attach). resp type: %x\n", resp->type);
        return 0;
    }

    // Transfer to Host
    while (kvc->main_buf_slot - kvc->worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = kvc->main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_transfer_to_host_3d* t = (struct virtio_gpu_transfer_to_host_3d*)&kvc->cmd_buf[slot];
    memset(t, 0, sizeof(*t));
    t->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D;
    t->hdr.ctx_id = ctx->ctx_id;
    t->resource_id = res_id;
    t->w = atlas_w;
    t->h = atlas_total_h;
    t->d = 1;
    virtio_submit_sync(kvc, t, kvc->cmd_buf_phys[slot], sizeof(*t), &kvc->resp_buf[slot], kvc->resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    resp = (struct virtio_gpu_ctrl_hdr*)&kvc->resp_buf[slot];
    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_printf("[VIRTIO:PYRION] Font upload failed (during transfer). resp type: %x\n", resp->type);
        return 0;
    }

    return res_id;
}

void pyrion_rect_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (ctx->controller_idx >= controller_count || !controllers) return;
	virtio_controller* kvc = &controllers[ctx->controller_idx];
	
	if (kvc->acceleration_present != 1) {
        if (!ctx->fb.addr) return;
        fb_draw_rect(&ctx->fb, x, y, w, h, rgba_to_u32(r, g, b, a));
        return;
    }
    uint32_t scissor_args[2];
    scissor_args[0] = (y << 16) | x;
    scissor_args[1] = ((y + h) << 16) | (x + w);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
    
    float color[4] = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    uint32_t args[5];
    args[0] = 0x7;
    memcpy(&args[1], color, sizeof(color));

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);

    scissor_args[0] = (ctx->viewport.y << 16) | ctx->viewport.x;
    scissor_args[1] = ((ctx->viewport.y + ctx->viewport.height) << 16) | (ctx->viewport.x + ctx->viewport.width);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
}
