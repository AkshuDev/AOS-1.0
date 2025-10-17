#include <inttypes.h>
#include <asm.h>

#include <inc/pcie.h>
#include <inc/gpu.h>
#include <inc/io.h>
#include <inc/mm/avmf.h>

static inline void svga_write(uintptr_t mmio_base, uint32_t index, uint32_t value, uint32_t svga_index_port, uint32_t svga_value_port) {
    *(volatile uint32_t*)(mmio_base + svga_index_port * 4) = index;
    *(volatile uint32_t*)(mmio_base + svga_value_port * 4) = value;
}

static inline uint32_t svga_read(uintptr_t mmio_base, uint32_t index, uint32_t svga_index_port, uint32_t svga_value_port) {
    *(volatile uint32_t*)(mmio_base + svga_index_port * 4) = index;
    return *(volatile uint32_t*)(mmio_base + svga_value_port * 4);
}

void vmware_init(struct gpu_device* gpu) {
    const uint32_t REG_ENABLE = 1;
    const uint32_t SVGA_ID_2 = 0x90002;
    const uint32_t INDEX_PORT = 0;
    const uint32_t VALUE_PORT = 1;
    const uint32_t REG_ID = 0;

    PCIe_FB* fb = gpu->framebuffer;

    pcie_device_t* dev = gpu->pcie_device;
    svga_write(fb->mmio_base, REG_ID, SVGA_ID_2, INDEX_PORT, VALUE_PORT);
    svga_write(fb->mmio_base, REG_ENABLE, 1, INDEX_PORT, VALUE_PORT);
}


void vmware_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    uintptr_t mmio = gpu->framebuffer->mmio_base;

    const uint32_t INDEX_PORT = 0;
    const uint32_t VALUE_PORT = 1;
    const uint32_t REG_ID = 0;
    const uint32_t REG_ENABLE = 1;
    const uint32_t REG_WIDTH = 2;
    const uint32_t REG_HEIGHT = 3;
    const uint32_t REG_BITS_PER_PIXEL = 7;
    const uint32_t REG_FB_START = 16;
    const uint32_t REG_FB_SIZE = 18;
    const uint32_t SVGA_ID_2 = 0x90002;

    svga_write(mmio, REG_ID, SVGA_ID_2, INDEX_PORT, VALUE_PORT);
    svga_write(mmio, REG_WIDTH, w, INDEX_PORT, VALUE_PORT);
    svga_write(mmio, REG_HEIGHT, h, INDEX_PORT, VALUE_PORT);
    svga_write(mmio, REG_BITS_PER_PIXEL, bpp, INDEX_PORT, VALUE_PORT);
    svga_write(mmio, REG_ENABLE, 1, INDEX_PORT, VALUE_PORT);

    gpu->framebuffer->w = w;
    gpu->framebuffer->h = h;
    gpu->framebuffer->bpp = bpp;
}

// Notify virtio
static void virtio_notify(uint32_t mmio_base, uint16_t queue_index) {
    // Write the queue index to the notification offset
    volatile uint16_t* notify = (volatile uint16_t*)(mmio_base + 0x50);
    *notify = queue_index;
}

// Add a descriptor index to the available ring
static void virtqueue_add(struct virtqueue* vq, uint16_t index) {
    //uint16_t idx = vq->avail[0] % vq->size;
    //vq->avail[idx+1] = index;
    //vq->avail[0]++;  // increment avail index
}

// Check if the GPU has used a descriptor (simplified)
static int virtqueue_used(struct virtqueue* vq) {
    //return vq->used[0] > 0;
}

void virtio_init(struct gpu_device* gpu) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    
    asm_outb(dev->bar0 + VIRTIO_PCI_DEVICE_STATUS, 0); //reset
    asm_outb(dev->bar0 + VIRTIO_PCI_DEVICE_STATUS, 1); //acknowledge
    asm_outb(dev->bar0 + VIRTIO_PCI_DEVICE_STATUS, 3); //driver
    
    // Pick the first enabled scanout
    uint32_t xres = 1024;
    uint32_t yres = 768;

    int found = 0;
    for (int i = 0; i < 1000000; i++) {
        if (scanouts[i % 4].enabled == 1) {
            fb->w = scanouts[i % 4].width;
            fb->h = scanouts[i % 4].height;
            found = 1;
            break;
        }
    }

    serial_printf("XRES: %u YRES: %u\n", xres, yres);

    // Map framebuffer and compute pitch
    fb->w = xres;
    fb->h = yres;
    fb->bpp = 32;
    fb->pitch = ((fb->w * (fb->bpp / 8) + 63) & ~63); // align to 64 bytes
    fb->size = fb->pitch * fb->h;
}
void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;

    //volatile struct virtio_gpu_config* config = (volatile struct virtio_gpu_config*)fb->mmio_base;
    //volatile struct virtio_gpu_display* display = (volatile struct virtio_gpu_display*)config + 1;

    //display->width = w;
    //display->height = h;

    fb->w = w;
    fb->h = h;
}

void get_framebuffer_info_vmware(PCIe_FB* fb, pcie_device_t* device, gpu_device_t* gpu) {
    if (device->vendor_id != VMware_VENDORID) return;
    //Class Code: 0x0300
    //Vendor ID: 0x15AD
    //Device ID: 0x0405
    //BAR0: Framebuffer
    //BAR1: I/O Registers
    uint32_t bar1 = pcie_read_bar(device->bus, device->slot, device->func, 1);
    fb->mmio_base = bar1;

    const uint32_t INDEX_PORT = 0;
    const uint32_t VALUE_PORT = 1;
    const uint32_t REG_ID = 0;
    const uint32_t REG_ENABLE = 1;
    const uint32_t REG_WIDTH = 2;
    const uint32_t REG_HEIGHT = 3;
    const uint32_t REG_BITS_PER_PIXEL = 7;
    const uint32_t REG_FB_START = 16;
    const uint32_t REG_FB_SIZE = 18;
    const uint32_t SVGA_ID_2 = 0x90002;

    uint32_t width = svga_read(bar1, REG_WIDTH, INDEX_PORT, VALUE_PORT);
    uint32_t height = svga_read(bar1, REG_HEIGHT, INDEX_PORT, VALUE_PORT);
    uint32_t bpp = svga_read(bar1, REG_BITS_PER_PIXEL, INDEX_PORT, VALUE_PORT);
    uint32_t fbsize = svga_read(bar1, REG_FB_SIZE, INDEX_PORT, VALUE_PORT);

    fb->w = width;
    fb->h = height;
    fb->bpp = bpp;
    fb->size = fbsize;
    fb->pitch = width * (bpp / 8);

    gpu->name = "VMware SVGA II";
    gpu->pcie_device = device;
    gpu->framebuffer = fb;
    gpu->init = vmware_init;
    gpu->swap_buffers = NULL;
    gpu->set_mode = vmware_set_mode;
}

void get_framebuffer_info_virtio(PCIe_FB* fb, pcie_device_t* device, gpu_device_t* gpu) {
    if (device->vendor_id != VirtIo_VENDORID) return;
    //Class Code: ?
    //Vendor ID: 0x1AF4
    //Device ID: 0x1050
    //BAR0: MMIO
    //BAR1: Framebuffer

    uint32_t bar0 = pcie_read_bar(device->bus, device->slot, device->func, 0);
    uint32_t bar1 = pcie_read_bar(device->bus, device->slot, device->func, 1);

    uint64_t mmio_base = (uint64_t)(bar0 & ~0xF);
    uint64_t fb_base = (uint64_t)(bar1 & ~0xF); 

    fb->phys = fb_base;
    fb->mmio_base = mmio_base;

    gpu->name = "VirtIo";
    gpu->pcie_device = device;
    gpu->framebuffer = fb;
    gpu->init = virtio_init;
    gpu->set_mode = virtio_set_mode;
    gpu->swap_buffers = NULL;
}

uint64_t gpu_get_framebuffer_and_info(PCIe_FB* fb, pcie_device_t* dev, gpu_device_t* gpu) {
    uint8_t bus, slot, func;
    uint32_t bar_value = 0;

    for (bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (slot = 0; slot < PCI_MAX_SLOT; slot++) {
            for (func = 0; func < PCI_MAX_FUNC; func++) {
                uint32_t data = pcie_read(bus, slot, func, 0);
                if (data == 0xFFFFFFFF) continue;

                uint16_t vendor = data & 0xFFFF;
                uint16_t device = (data >> 16) & 0xFFFF;
                if (vendor == 0xFFFF) continue;

                uint32_t class_data = pcie_read(bus, slot, func, 0x08);
                uint8_t class_code = (class_data >> 24) & 0xFF;
                uint8_t subclass   = (class_data >> 16) & 0xFF;

                if (class_code == PCI_CLASS_DISPLAY || class_code == PCI_VGA_DISPLAY) {
                    // Found a Display controller
                    for (int bar = 0; bar < PCI_BAR_COUNT; bar++) {
                        bar_value = pcie_read_bar(bus, slot, func, bar);
                        if (bar_value == 0xFFFFFFFF || bar_value == 0)
                            continue; // skip invalid

                        if (bar_value & 1) 
                            continue; // I/O space, not memory

                        uint64_t fb_phys = (uint64_t)bar_value & ~0xFU; // mask flags
                        if (fb_phys) {
                            // check if this is a framebuffer-like address
                            if (fb_phys >= 0xE0000000) {
                                dev->vendor_id = vendor;
                                dev->device_id = device;
                                dev->class_code = class_code;
                                dev->subclass = subclass;
                                dev->prog_if = 0;
                                dev->bus = bus;
                                dev->slot = slot;
                                dev->func = func;
                                dev->bar0 = pcie_read_bar(bus, slot, func, 0);

                                if (vendor == VMware_VENDORID) {
                                    get_framebuffer_info_vmware(fb, dev, gpu);
                                } else if (vendor == VirtIo_VENDORID) {
                                    get_framebuffer_info_virtio(fb, dev, gpu);
                                }
                                fb->phys = fb_phys;

                                return fb_phys; // plausible framebuffer
                            }
                        }
                    }
                }
            }
        }
    }

    fb->phys = 0xE0000000;
    fb->mmio_base = 0;
    fb->w = 640;
    fb->h = 480;
    fb->bpp = 32;
    fb->pitch = fb->w * 4;
    fb->size = fb->h * fb->pitch;

    // Fallback for Bochs/QEMU stdvga
    return (uint64_t)0xE0000000;
}
