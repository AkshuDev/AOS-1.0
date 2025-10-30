#include <inttypes.h>
#include <asm.h>

#include <inc/pcie.h>
#include <inc/gpu.h>

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