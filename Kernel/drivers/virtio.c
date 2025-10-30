#include <inttypes.h>
#include <asm.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/virtio.h>
#include <inc/kfuncs.h>
#include <inc/io.h>
#include <inc/gpu.h>
#include <inc/pcie.h>

static uint8_t vq_buf[0x1000] __attribute__((aligned(4096)));
static struct virtqueue virtq;

static struct virtio_gpu_ctrl_hdr* cmd_buf;
static struct virtio_gpu_resp_display_info* resp_buf;

void virtio_flush(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    PCIe_FB* fb = gpu->framebuffer;
    uint32_t bar0 = gpu->pcie_device->bar0 & ~0xF;

    // Fill flush command
    struct virtio_gpu_resource_flush* flush = (void*)cmd_buf;
    memset(flush, 0, sizeof(*flush));
    flush->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush->resource_id = 1; // resource created during init
    flush->x = x;
    flush->y = y;
    flush->width = w;
    flush->height = h;

    // Fill descriptor table
    virtq.desc[0].addr = (uintptr_t)flush;
    virtq.desc[0].len = sizeof(*flush);
    virtq.desc[0].flags = 0; // device reads
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_buf;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2; // device writes
    virtq.desc[1].next = 0;

    // Add to avail ring
    virtq.avail->ring[virtq.avail->idx % virtq.size] = 0; // descriptor 0
    virtq.avail->idx++;

    // Notify device
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    // Wait for device to process command
    uint16_t idx_before = virtq.used->idx;
    while (virtq.used->idx == idx_before);
}

// Total hours wasted on virtio: 5
void virtio_init(struct gpu_device* gpu) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    
    uint32_t bar0 = dev->bar0 & ~0xF;
    fb->mmio_base = bar0;

    asm_outb(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x00);
    asm_outb(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x01); // Acknowledge
    asm_outb(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x03); // Driver

    // Select queue
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_SEL, VIRTIO_GPU_QUEUE_INDEX);
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NUM, GPU_VIRTQUEUE_SIZE);

    serial_print("Setting VirtQueue...\n");

    memset(&virtq, 0, sizeof(virtq));
    virtq.desc = (struct virtq_desc*)(vq_buf);
    virtq.avail = (struct virtq_avail*)((uint8_t*)virtq.desc + sizeof(struct virtq_desc) * GPU_VIRTQUEUE_SIZE);
    virtq.used = (struct virtq_used*)((uintptr_t)virtq.avail + sizeof(uint16_t) * (2 + GPU_VIRTQUEUE_SIZE));
    virtq.size = GPU_VIRTQUEUE_SIZE;

    // Set PFN (physical frame number)
    uintptr_t vq_phys = (uintptr_t)vq_buf;
    asm_outl(bar0 + VIRTIO_PCI_QUEUE_PFN, vq_phys >> 12);
    serial_print("Setted Virtqueue!\n");

    // Finish setup
    asm_outb(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x07); // DRIVER_OK

    // Allocate buffers (1 page each)
    serial_print("Allocating buffers...\n");
    cmd_buf = (struct virtio_gpu_ctrl_hdr*)avmf_alloc_region(0x1000, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);
    resp_buf = (struct virtio_gpu_resp_display_info*)avmf_alloc_region(0x1000, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);
    memset(cmd_buf, 0, sizeof(*cmd_buf));
    memset(resp_buf, 0, sizeof(*resp_buf));

    cmd_buf->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    // Fill descriptor table
    virtq.desc[0].addr = (uintptr_t)cmd_buf;
    virtq.desc[0].len = sizeof(*cmd_buf);
    virtq.desc[0].flags = 0; // Device reads
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_buf;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2; // Device writes
    virtq.desc[1].next = 0;

    // Put in avail ring
    virtq.avail->ring[0] = 0;
    virtq.avail->idx++;

    // Notify
    serial_print("Notifing...\n");
    uint16_t idx_before = virtq.used->idx;
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    // Wait for device response
    while (virtq.used->idx == idx_before);

    if (resp_buf->displays[0].enabled) {
        fb->w = resp_buf->displays[0].width;
        fb->h = resp_buf->displays[0].height;
        fb->bpp = 32;
        fb->pitch = fb->w * 4;
        fb->size = fb->pitch * fb->h;
    } else {
        fb->w = 800;
        fb->h = 600;
        fb->bpp = 32;
        fb->pitch = fb->w * 4;
        fb->size = fb->pitch * fb->h;
    }

    serial_print("Creating 2D Resource...\n");

    // Create 2D Resource
    struct virtio_gpu_resource_create_2d* create = (void*)cmd_buf;
    memset(create, 0, sizeof(*create));
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = 1;
    create->format = 1; // B8G8R8A8_UNORM
    create->width = fb->w;
    create->height = fb->h;

    // Send
    serial_print("Sending...\n");
    virtq.desc[0].addr = (uintptr_t)create;
    virtq.desc[0].len = sizeof(*create);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_buf;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;

    idx_before = virtq.used->idx;
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    while (virtq.used->idx == idx_before); // Wait

    // Attach framebuffer memory
    serial_print("Attaching framebuffer memory...\n");
    struct virtio_gpu_resource_attach_backing* attach = (void*)cmd_buf;
    memset(attach, 0, sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry));
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = 1;
    attach->nr_entries = 1;
    attach->entries[0].addr = (uint64_t)(uintptr_t)fb->phys;
    attach->entries[0].length = fb->size;

    // Send
    serial_print("Sending...\n");
    virtq.desc[0].addr = (uintptr_t)attach;
    virtq.desc[0].len = sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    while (virtq.used->idx == 2); // Wait

    virtio_flush(gpu, 0, 0, fb->w, fb->h);
}
void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    return;
}