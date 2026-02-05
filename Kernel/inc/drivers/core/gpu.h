#pragma once

#include <inttypes.h>
#include <asm.h>
#include <inc/core/pcie.h>

#define Bochs_VENDORID 0x1234
#define VMware_VENDORID 0x15AD
#define Intel_VENDORID 0x8086
#define Nvidia_VENDORID 0x10DE
#define AMD_VENDORID 0x1002
#define VirtIo_VENDORID 0x1AF4

typedef struct gpu_device {
    const char* name;

    pcie_device_t* pcie_device;
    PCIe_FB* framebuffer;

    // Function pointers
    void (*init)(struct gpu_device* gpu);
    void (*init_resources)(struct gpu_device* gpu, int resource_id);
    void (*set_mode)(struct gpu_device* gpu, uint32_t width, uint32_t height, uint32_t bpp);
    void (*swap_buffers)(struct gpu_device* gpu);
    void (*flush)(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id);
} gpu_device_t;

void svga_write(uintptr_t mmio_base, uint32_t index, uint32_t value, uint32_t svga_index_port, uint32_t svga_value_port) __attribute__((used));
uint32_t svga_read(uintptr_t mmio_base, uint32_t index, uint32_t svga_index_port, uint32_t svga_value_port) __attribute__((used));

uint64_t gpu_get_framebuffer_and_info(PCIe_FB* fb, pcie_device_t* dev, gpu_device_t* gpu) __attribute__((used));
