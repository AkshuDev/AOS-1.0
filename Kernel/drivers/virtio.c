#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/virtio.h>
#include <inc/kfuncs.h>
#include <inc/io.h>
#include <inc/gpu.h>
#include <inc/pcie.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

static uint8_t vq_buf[0x1000] __attribute__((aligned(4096)));
static struct virtqueue virtq;

static struct virtio_gpu_ctrl_hdr* cmd_buf;
static struct virtio_gpu_resp_display_info* resp_buf;
static uintptr_t notify_base;
static uint32_t notify_multiplier;

static volatile struct virtio_common_cfg* common_cfg;

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

static struct virtio_cap get_cap(uint8_t b, uint8_t s, uint8_t f, uint8_t target_type) {
    struct virtio_cap cap = {0};
    uint8_t cap_ptr = pcie_read(b, s, f, 0x34) & 0xFF;
    while (cap_ptr != 0) {
        uint32_t cap_hdr = pcie_read(b, s, f, cap_ptr);
        uint8_t cap_id = cap_hdr & 0xFF;

        if (cap_id == 0x09) { // Vendor Specific
            uint8_t type = (pcie_read(b, s, f, cap_ptr + 3) >> 24) & 0xFF;
            if (type == target_type) {
                cap.cap_ptr = cap_ptr;
                cap.bar = (pcie_read(b, s, f, cap_ptr + 4) >> 0) & 0xFF;
                cap.offset = pcie_read(b, s, f, cap_ptr + 8);
                cap.length = pcie_read(b, s, f, cap_ptr + 12);
                return cap;
            }
        }
        cap_ptr = (cap_hdr >> 8) & 0xFF;
    }
    return cap;
}

static void setup_queue(gpu_device_t* gpu, uint16_t q_idx) {
    common_cfg->queue_select = q_idx;
    uint16_t size = common_cfg->queue_size;
    size_t desc_t_size = size * sizeof(struct virtq_desc);
    size_t avail_r_size = 6 + (2 * size);
    size_t used_r_size = 6 + (8 * size);

    uintptr_t phys = avmf_alloc_phys_contiguous(desc_t_size + avail_r_size + used_r_size);
    void* mem = (void*)avmf_map_phys_to_virt( AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);

    virtq.desc = (struct virtq_desc*)mem;
    virtq.avail = (struct virtq_avail*)((uintptr_t)mem + desc_t_size);
    virtq.used = (struct virtq_used*)ALIGN_UP((uintptr_t)virtq.avail + avail_r_size, 4096);
    virtq.queue_size = size;

    common_cfg->queue_desc = phys;
    common_cfg->queue_avail = phys + desc_t_size;
    common_cfg->queue_used = avmf_virt_to_phys((uintptr_t)virtq.used);
    common_cfg->queue_enable = 1;
}

void virtio_flush(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)cmd_buf;
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->x = x;
    f->y = y;
    f->width = w;
    f->height = h;
    f->resource_id = 1;

    uint16_t head = virtq.free_head;
    // Map to physical address so the GPU can see it
    virtq.desc[head].addr = avmf_virt_to_phys((uintptr_t)f);
    virtq.desc[head].len = sizeof(*f);
    virtq.desc[head].flags = 0; 

    virtq.avail->ring[virtq.avail->idx % virtq.queue_size] = head;

    // Memory fence to ensure the device sees the ring update before the notification
    __asm__ volatile("" : : : "memory"); 
 
    virtq.avail->idx++;

    // Calculate notification address for Queue 0
    common_cfg->queue_select = 0;
    uintptr_t db = notify_base + (common_cfg->queue_notify_off * notify_multiplier);
    mmio_write32(db, 0);
}

// Total hours wasted on virtio: 17 (LET ME COOK)
void virtio_init(struct gpu_device* gpu) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uintptr_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing...\n");

    struct virtio_cap common_cap = get_cap(dev->bus, dev->slot, dev->func, 1);
    uint32_t bar_val = pcie_read_bar(dev->bus, dev->slot, dev->func, common_cap.bar);
    uintptr_t bar_phys = bar_val & ~0xF;
    uintptr_t common_cfg_phys = bar_phys + common_cap.offset;
    common_cfg = (volatile struct virtio_common_cfg*)(common_cfg_phys + AOS_DIRECT_MAP_BASE);

    struct virtio_cap notify_cap = get_cap(dev->bus, dev->slot, dev->func, 2);
    uint32_t n_bar_val = pcie_read_bar(dev->bus, dev->slot, dev->func, notify_cap.bar);
    notify_base = (n_bar_val & ~0xF) + notify_cap.offset + AOS_DIRECT_MAP_BASE;
    notify_multiplier = pcie_read(dev->bus, dev->slot, dev->func, notify_cap.cap_ptr + 16);

    // RESET
    common_cfg->device_status = 0;
    common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    // Features
    common_cfg->device_feature_select = 0;
    uint32_t features = common_cfg->device_feature;
    common_cfg->device_feature_select = 0;
    common_cfg->driver_feature = features;

    common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_print("[VIRTIO] Failed to negotiate features!\n");
        return;
    }

    // setup Queue
    setup_queue(gpu, 0);
}

void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    (void)gpu; (void)w; (void)h; (void)bpp;
}
