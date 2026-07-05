#include <aos_inttypes.h>
#include <asm.h>

#include <inc/core/pcie.h>
#include <inc/core/module.h>
#include <inc/core/kfuncs.h>

#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/io/io.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/vmware.h>
#include <inc/drivers/gpu/virtio.h>

void svga_write(uintptr_t mmio_base, uint32_t index, uint32_t value, uint32_t svga_index_port, uint32_t svga_value_port) {
    *(volatile uint32_t*)(mmio_base + svga_index_port * 4) = index;
    *(volatile uint32_t*)(mmio_base + svga_value_port * 4) = value;
}

uint32_t svga_read(uintptr_t mmio_base, uint32_t index, uint32_t svga_index_port, uint32_t svga_value_port) {
    *(volatile uint32_t*)(mmio_base + svga_index_port * 4) = index;
    return *(volatile uint32_t*)(mmio_base + svga_value_port * 4);
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
    gpu->init = NULL;
    gpu->swap_buffers = NULL;
    gpu->set_mode = NULL;
    gpu->flush = NULL;
}

void get_framebuffer_info_virtio(PCIe_FB* fb, pcie_device_t* device, gpu_device_t* gpu) {
    if (device->vendor_id != VirtIo_VENDORID) return;

    uint32_t bar0 = pcie_read_bar(device->bus, device->slot, device->func, 0);
    uint32_t bar1 = pcie_read_bar(device->bus, device->slot, device->func, 1);

    uint64_t mmio_base = (uint64_t)(bar0 & ~0xF);
    uint64_t fb_base = (uint64_t)(bar1 & ~0xF); 

    fb->phys = fb_base;
    fb->mmio_base = mmio_base;

    gpu->pcie_device = device;
    gpu->framebuffer = fb;
}

aos_bool gpu_find_gpu(PCIe_FB* fb, struct AOS_Module** out) {
    struct AOS_Module* m = module_get_first_applicable_registered_driver(PCI_VGA_DISPLAY, PCI_SUBCLASS_VGA, 1, 0, 0, 0, 0, 0, 0);

    if (!m) {
        serial_print("[GPU Controller] Did not find any registered GPUs!\n");
        return AOS_FALSE;
    }
    if (m->hdr.type != MODULE_TYPE_DRIVER || m->Modules.driver_module.type != MODULE_DRIVER_TYPE_GPU) {
        serial_print("[GPU Controller] Did not find any registered GPU drivers!\n");
        return AOS_FALSE;
    }

    switch (m->Modules.driver_module.pcie_device.vendor_id) {
        case VirtIo_VENDORID: {
            get_framebuffer_info_virtio(fb, &m->Modules.driver_module.pcie_device, &m->Modules.driver_module.DriverConnections.gpu_connector);
            break;
        }
        default: {
            serial_printf("[GPU Controller] Error: Unknown VendorID (%d) Based Driver!\n", m->Modules.driver_module.pcie_device.vendor_id);
            return AOS_FALSE;
        }
    }

	*out = m;

    serial_printf("[GPU Controller] Using '%s' driver\n", m->hdr.name);
    return AOS_TRUE;
}
