#include <inttypes.h>
#include <asm.h>

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

static inline void mmio_write32(uint32_t addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}
static inline uint32_t mmio_read32(uint32_t addr) {
    return *(volatile uint32_t*)addr;
}
static inline void mmio_write16(uint32_t addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}
static inline uint16_t mmio_read16(uint32_t addr) {
    return *(volatile uint16_t*)addr;
}
static inline void mmio_write8(uint32_t addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}
static inline uint8_t mmio_read8(uint32_t addr) {
    return *(volatile uint8_t*)addr;
}


void virtio_flush(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    PCIe_FB* fb = gpu->framebuffer;
    uint32_t bar0 = gpu->pcie_device->bar0 & ~0xF;

    struct virtio_gpu_resource_flush* flush = (void*)cmd_buf;
    memset(flush, 0, sizeof(*flush));
    flush->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush->resource_id = 1;
    flush->x = x; 
    flush->y = y;
    flush->width = w;
    flush->height = h;

    virtq.desc[0].addr = (uintptr_t)flush;
    virtq.desc[0].len = sizeof(*flush);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_buf;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2;
    virtq.desc[1].next = 0;

    virtq.avail->ring[virtq.avail->idx % virtq.size] = 0;
    virtq.avail->idx++;

    *(volatile uint16_t*)(bar0 + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    uint16_t idx_before = virtq.used->idx;
    for (volatile int timeout = 0; timeout < 100000000; timeout++) {
        if (virtq.used->idx != idx_before)
            break;
    }
    if (virtq.used->idx == idx_before) serial_print("[VIRTIO DRIVER] Flush timed out\n");
    serial_print("[VIRTIO DRIVER] Flushed!\n");
}

// Total hours wasted on virtio: 14.5
void virtio_init(struct gpu_device* gpu) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uint32_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing...\n");

    // Reset
    mmio_write8(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x00);

    // Acknowledge & Driver
    mmio_write8(bar0 + VIRTIO_PCI_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write8(bar0 + VIRTIO_PCI_DEVICE_STATUS, mmio_read8(bar0 + VIRTIO_PCI_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER);

    // Feature negotiation
    uint32_t host_features = mmio_read32(bar0 + VIRTIO_PCI_HOST_FEATURES);
    uint32_t guest_features = host_features & VIRTIO_FEATURE_MASK;
    mmio_write32(bar0 + VIRTIO_PCI_GUEST_FEATURES, guest_features);
    mmio_write8(bar0 + VIRTIO_PCI_DEVICE_STATUS, mmio_read8(bar0 + VIRTIO_PCI_DEVICE_STATUS) | VIRTIO_STATUS_FEATURES_OK);

    if (!(mmio_read8(bar0 + VIRTIO_PCI_DEVICE_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        serial_print("[VIRTIO DRIVER] Feature negotiation failed\n");
        return;
    }

    // Queue setup
    mmio_write16(bar0 + VIRTIO_PCI_QUEUE_SEL, 0);
    mmio_write16(bar0 + VIRTIO_PCI_QUEUE_NUM, GPU_VIRTQUEUE_SIZE);

    memset(&virtq, 0, sizeof(virtq));
    phys_addr_t vq_phys = avmf_alloc_phys_contiguous(PAGE_SIZE);
    if (vq_phys & 0xFFF) serial_print("[VIRTIO DRIVER] Virtqueue not aligned!\n");

    virtq.desc  = (struct virtq_desc*)vq_buf;
    virtq.avail = (struct virtq_avail*)((uint8_t*)virtq.desc + sizeof(struct virtq_desc) * GPU_VIRTQUEUE_SIZE);
    virtq.used  = (struct virtq_used*)ALIGN_UP((uintptr_t)virtq.avail + sizeof(uint16_t) * (2 + GPU_VIRTQUEUE_SIZE), 4);
    virtq.size  = GPU_VIRTQUEUE_SIZE;

    serial_print("[VIRTIO DRIVER] Allocating...\n");
    virt_addr_t vq_virt = avmf_map_phys_to_virt(vq_phys, PAGE_SIZE, PAGE_PRESENT | PAGE_RW);
    serial_print("[VIRTIO DRIVER] Mapping Page...\n");
    pager_map(vq_virt, vq_phys, PAGE_PRESENT | PAGE_RW);
    serial_print("[VIRTIO DRIVER] Doing work...\n");
    mmio_write32(bar0 + VIRTIO_PCI_QUEUE_PFN, vq_phys >> 12);

    // DRIVER_OK    
    __asm__ volatile("sfence" ::: "memory");
    mmio_write8(bar0 + VIRTIO_PCI_DEVICE_STATUS, mmio_read8(bar0 + VIRTIO_PCI_DEVICE_STATUS) | VIRTIO_STATUS_DRIVER_OK);

    serial_print("[VIRTIO DRIVER] VirtQueue Ready!\n");

    // Allocate buffers
    phys_addr_t big_buff_phys = avmf_alloc_phys_contiguous(0x3000);
    virt_addr_t big_buff = avmf_map_phys_to_virt(big_buff_phys, 0x3000, PAGE_PRESENT | PAGE_RW);

    phys_addr_t cmd_phys = big_buff_phys;
    phys_addr_t resp_phys = (phys_addr_t)(big_buff_phys + 0x2000);
    cmd_buf = (struct virtio_gpu_ctrl_hdr*)big_buff;
    
    serial_print("[VIRTIO DRIVER] mapped physical memory for cmd_buf!\n");
    serial_printf("[VIRTIO GPU] cmd_buf: virt=%p phys=%llx\n", (void*)cmd_buf, cmd_phys);
    memset(cmd_buf, 0, 0x2000);
    
    resp_buf = (struct virtio_gpu_resp_display_info*)(big_buff + 0x2000);
    memset(resp_buf, 0, 0x1000);
    
    serial_print("[VIRTIO DRIVER] mapped physical memory for resp_buf!\n");
    serial_printf("[VIRTIO GPU] resp_buf: virt=%p phys=%llx\n", (void*)resp_buf, resp_phys);

    serial_print("[VIRTIO DRIVER] Getting Display Info...\n");

    // Get display info
    cmd_buf->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    virtq.desc[0].addr = (uintptr_t)cmd_phys;
    virtq.desc[0].len = sizeof(*cmd_buf);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_phys;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2;
    virtq.desc[1].next = 0;

    virtq.avail->ring[0] = 0;
    virtq.avail->idx++;

    *(volatile uint16_t*)(bar0 + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    uint16_t idx_before = virtq.used->idx;
    for (volatile int timeout = 0; timeout < 100000000; timeout++) {
        if (virtq.used->idx != idx_before)
            break;
    }
    if (virtq.used->idx == idx_before)
        serial_print("[VIRTIO DRIVER] GET_DISPLAY_INFO timed out\n");

    // Set framebuffer parameters
    if (resp_buf->displays[0].enabled) {
        fb->w = resp_buf->displays[0].width;
        fb->h = resp_buf->displays[0].height;
    } else {
        fb->w = 800;
        fb->h = 600;
    }
    fb->bpp = 32;
    fb->pitch = fb->w * 4;
    fb->size = fb->pitch * fb->h;

    serial_print("[VIRTIO DRIVER] Display info OK\n");

    // Create resource
    struct virtio_gpu_resource_create_2d* create = (void*)cmd_buf;
    memset(create, 0, sizeof(*create));
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = 1;
    create->format = 1; // B8G8R8A8_UNORM
    create->width = fb->w;
    create->height = fb->h;

    virtq.desc[0].addr = (uintptr_t)cmd_phys;
    virtq.desc[0].len = sizeof(*create);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_phys;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;

    *(volatile uint16_t*)(bar0 + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    idx_before = virtq.used->idx;
    for (volatile int timeout = 0; timeout < 100000000; timeout++) {
        if (virtq.used->idx != idx_before)
            break;
    }
    if (virtq.used->idx == idx_before)
        serial_print("[VIRTIO DRIVER] create_2d timed out\n");
    
    serial_print("[VIRTIO DRIVER] Resources Created!\n[VIRTIO DRIVER] Attaching Framebuffer memory...\n");

    // Attach framebuffer memory
    struct virtio_gpu_resource_attach_backing* attach = (void*)cmd_buf;
    memset(attach, 0, sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry));
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = 1;
    attach->nr_entries = 1;
    attach->entries[0].addr = (uint64_t)(uintptr_t)fb->phys;
    attach->entries[0].length = fb->size;

    virtq.desc[0].addr = (uintptr_t)attach;
    virtq.desc[0].len = sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;

    *(volatile uint16_t*)(bar0 + VIRTIO_PCI_QUEUE_NOTIFY) = 0;

    idx_before = virtq.used->idx;
    for (volatile int timeout = 0; timeout < 100000000; timeout++) {
        if (virtq.used->idx != idx_before)
            break;
    }
    if (virtq.used->idx == idx_before)
        serial_print("[VIRTIO DRIVER] attach_backing timed out\n");

    serial_print("[VIRTIO DRIVER] Done!\n[VIRTIO DRIVER] Flushing...\n");

    virtio_flush(gpu, 0, 0, fb->w, fb->h);
    serial_print("[VIRTIO DRIVER] Initialization done!\n");
}

void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    (void)gpu; (void)w; (void)h; (void)bpp;
}