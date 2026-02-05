#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/virtio.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/core/gpu.h>
#include <inc/core/pcie.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

static uint8_t vq_buf[0x1000] __attribute__((aligned(4096)));
static struct virtqueue virtq;

static struct virtio_gpu_ctrl_hdr* cmd_buf;
static struct virtio_gpu_resp_display_info* resp_buf;
static uint64_t cmd_buf_phys;
static uint64_t resp_buf_phys;

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

static void virtio_submit_sync(void* cmd, uint64_t cmd_phys, size_t cmd_size, void* resp, uint64_t resp_phys, size_t resp_size) {
    uint16_t head = virtq.free_head;
    uint16_t next = (head + 1) % virtq.queue_size;

    // Descriptor 1: The Command (Read-only)
    virtq.desc[head].addr = (uintptr_t)cmd_phys;
    virtq.desc[head].len = cmd_size;
    virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    virtq.desc[head].next = next;

    // Descriptor 2: The Response (Write-only for GPU)
    virtq.desc[next].addr = (uintptr_t)resp_phys;
    virtq.desc[next].len = resp_size;
    virtq.desc[next].flags = VIRTQ_DESC_F_WRITE;
    virtq.desc[next].next = 0;

    virtq.avail->ring[virtq.avail->idx % virtq.queue_size] = head;
    __asm__ volatile("sfence" ::: "memory"); // Ensure GPU sees RAM update
    virtq.avail->idx++;

    // Notify Doorbell
    uintptr_t db = notify_base + (common_cfg->queue_notify_off * notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    while (virtq.used->idx != virtq.avail->idx) {
        __asm__ volatile("pause");
    }

    virtq.free_head = (next + 1) % virtq.queue_size;
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
    serial_print("[VIRTIO] Setting Queues....\n");

    common_cfg->queue_select = q_idx;
    uint16_t size = common_cfg->queue_size;
    size_t desc_t_size = size * sizeof(struct virtq_desc);
    size_t avail_r_size = 6 + (2 * size);
    size_t used_r_size = 6 + (8 * size);

    uint64_t phys = 0;
    void* mem = (void*)avmf_alloc(desc_t_size + avail_r_size + used_r_size, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &phys);
    if (!mem) {serial_print("[VIRTIO] Failed to Allocate Memory!\n"); return;}
    if (!phys) {serial_print("[VIRTIO] Failed to retrieve physical address!\n"); return;}

    virtq.desc = (struct virtq_desc*)mem;
    virtq.avail = (struct virtq_avail*)((uintptr_t)mem + desc_t_size);
    virtq.used = (struct virtq_used*)((uintptr_t)virtq.avail + avail_r_size);
    virtq.queue_size = size;

    common_cfg->queue_desc = phys;
    common_cfg->queue_avail = phys + desc_t_size;
    common_cfg->queue_used = (uintptr_t)phys + avail_r_size + desc_t_size;
    common_cfg->queue_enable = 1;
}

void virtio_flush(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id) {
    serial_print("[VIRTIO] Flushing...\n");
    uint64_t offset = (uint64_t)y * gpu->framebuffer->pitch + (uint64_t)x * (gpu->framebuffer->bpp / 8);
    struct virtio_gpu_transfer_to_host_2d* t = (struct virtio_gpu_transfer_to_host_2d*)cmd_buf;
    t->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    t->r.x = x;
    t->r.y = y;
    t->r.width = w;
    t->r.height = h;
    t->offset = offset;
    t->padding = 0;
    t->resource_id = resource_id;
    virtio_submit_sync(t, cmd_buf_phys, sizeof(*t), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_ctrl_hdr));

    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)cmd_buf;
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->r.x = x;
    f->r.y = y;
    f->r.width = w;
    f->r.height = h;
    f->resource_id = resource_id;
    f->padding = 0;

    virtio_submit_sync(f, cmd_buf_phys, sizeof(*f), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_ctrl_hdr));
    serial_print("[VIRTIO] Flushed!\n");
}

// Total hours wasted on virtio: 18 (I DID COOK)
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
    while (common_cfg->device_status != 0) { __asm__ volatile("pause"); }
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
    
    // setup buffers
    cmd_buf = (struct virtio_gpu_ctrl_hdr*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &cmd_buf_phys);
    resp_buf = (struct virtio_gpu_resp_display_info*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &resp_buf_phys);
    if (!cmd_buf || !resp_buf) {
        serial_print("[VIRTIO] Failed to allocate command and response buffers!\n");
        return;
    }

    // setup Queue
    setup_queue(gpu, 0);

    // Get fb info
    struct virtio_gpu_ctrl_hdr* get_info = (struct virtio_gpu_ctrl_hdr*)cmd_buf;
    get_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    virtio_submit_sync(get_info, cmd_buf_phys, sizeof(*get_info), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_resp_display_info));
    struct virtio_gpu_resp_display_info* display = (struct virtio_gpu_resp_display_info*)resp_buf;
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

    serial_print("[VIRTIO] Initialization completed!\n");
}

void virtio_init_resources(struct gpu_device* gpu, int id) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uintptr_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing Resources...\n");

    // make resources
    struct virtio_gpu_resource_create_2d* create = (struct virtio_gpu_resource_create_2d*)cmd_buf;
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = id;
    create->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    create->width = fb->w;
    create->height = fb->h;
    virtio_submit_sync(create, cmd_buf_phys, sizeof(*create), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_ctrl_hdr));

    struct virtio_gpu_resource_attach_backing* attach = (struct virtio_gpu_resource_attach_backing*)cmd_buf;
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = id;
    attach->nr_entries = 1;
    struct virtio_gpu_mem_entry* entry = (struct virtio_gpu_mem_entry*)((uintptr_t)attach + sizeof(*attach));
    entry->addr = fb->phys;
    entry->length = fb->size;
    virtio_submit_sync(attach, cmd_buf_phys, sizeof(*attach) + sizeof(*entry), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_ctrl_hdr));

    struct virtio_gpu_set_scanout* scanout = (struct virtio_gpu_set_scanout*)cmd_buf;
    scanout->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout->scanout_id = 0;
    scanout->resource_id = id;
    scanout->r.x = 0;
    scanout->r.y = 0;
    scanout->r.width = fb->w;
    scanout->r.height = fb->h;
    virtio_submit_sync(scanout, cmd_buf_phys, sizeof(*scanout), resp_buf, resp_buf_phys, sizeof(struct virtio_gpu_ctrl_hdr));

    serial_print("[VIRTIO] Initialization of resources completed!\n");
}

void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    (void)gpu; (void)w; (void)h; (void)bpp;
}
