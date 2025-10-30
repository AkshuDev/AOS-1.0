#include <inttypes.h>
#include <asm.h>

#include <mm/avef.h>
#include <mm/pager.h>

#include <inc/virtio.h>
#include <inc/kfuncs.h>
#include <inc/io.h>
#include <inc/gpu.h>
#include <inc/pcie.h>

static uint8_t vq_buf[0x1000] __attribute__((aligned(4096)));
static struct virtqueue virtq;

static struct virtio_gpu_ctrl_hdr* cmd_buf;
static struct virtio_gpu_resp_display_info* resp_buf;

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

    memset(&virtq, 0, sizeof(virtq));
    virtq.desc = (struct virtq_desc*)(vq_buf);
    virtq.avail = (struct virtq_avail*)(vq_buf + 0x100);
    virtq.used = (struct virtq_used*)(vq_buf + 0x200);
    virtq.size = GPU_VIRTQUEUE_SIZE;

    // Set PFN (physical frame number)
    uintptr_t vq_phys = (uintptr_t)vq_buf;
    asm_outl(bar0 + VIRTIO_PCI_QUEUE_PFN, vq_phys >> 12);

    // Finish setup
    asm_outb(bar0 + VIRTIO_PCI_DEVICE_STATUS, 0x07); // DRIVER_OK

    // Allocate buffers (1 page each)
    cmd_buf = (struct virtio_gpu_ctrl_hdr*)avef_alloc_region(0x1000, AVEF_FLAG_PRESENT | AVEF_FLAG_WRITEABLE);
    resp_buf = (struct virtio_gpu_resp_display_info*)avef_alloc_region(0x1000, AVEF_FLAG_PRESENT | AVEF_FLAG_WRITEABLE);
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
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    // Wait for device response
    while (virtq.used->idx == 0);

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

    // Create 2D Resource
    struct virtio_gpu_resource_create_2d* create = (void*)cmd_buf;
    memset(create, 0, sizeof(*create));
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = fb->resource_id;
    create->format = 1; // B8G8R8A8_UNORM
    create->width = fb->w;
    create->height = fb->h;

    // Send
    virtq.desc[0].addr = (uintptr_t)create;
    virtq.desc[0].len = sizeof(*create);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.desc[1].addr = (uintptr_t)resp_buf;
    virtq.desc[1].len = sizeof(*resp_buf);
    virtq.desc[1].flags = 2;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    while (virtq.used->idx == 1); // Wait

    // Attach framebuffer memory
    struct virtio_gpu_resource_attach_backing* attach = (void*)cmd_buf;
    memset(attach, 0, sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry));
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = fb->resource_id;
    attach->nr_entries = 1;
    attach->entries[0].addr = (uint64_t)(uintptr_t)fb->phys;
    attach->entries[0].length = fb->phys;

    // Send
    virtq.desc[0].addr = (uintptr_t)attach;
    virtq.desc[0].len = sizeof(*attach) + sizeof(struct virtio_gpu_mem_entry);
    virtq.desc[0].flags = 0;
    virtq.desc[0].next = 1;

    virtq.avail->ring[virtq.avail->idx % GPU_VIRTQUEUE_SIZE] = 0;
    virtq.avail->idx++;
    asm_outw(bar0 + VIRTIO_PCI_QUEUE_NOTIFY, 0);
    while (virtq.used->idx == 2); // Wait
}
void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    return;
}