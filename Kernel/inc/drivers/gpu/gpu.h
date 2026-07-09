#pragma once

#include <aos_inttypes.h>
#include <asm.h>
#include <inc/core/pcie.h>

#define Bochs_VENDORID 0x1234
#define VMware_VENDORID 0x15AD
#define Intel_VENDORID 0x8086
#define Nvidia_VENDORID 0x10DE
#define AMD_VENDORID 0x1002
#define VirtIo_VENDORID 0x1AF4

#define GPU_REFRESH_FLAG_CORE (1 << 0)
#define GPU_REFRESH_FLAG_DEVICE (1 << 1)
#define GPU_REFRESH_FLAG_BUFFERS (1 << 2)
#define GPU_REFRESH_FLAG_QUEUES (1 << 3)

#define GPU_REFRESH_ALL_FLAG (GPU_REFRESH_FLAG_CORE | GPU_REFRESH_FLAG_DEVICE | GPU_REFRESH_FLAG_BUFFERS | GPU_REFRESH_FLAG_QUEUES)

// APIs
#include <inc/drivers/gpu/apis/pyrion.h>

struct AOS_Module;

typedef struct gpu_device {
    const char* name;
	uint64_t controller_idx;

    pcie_device_t* pcie_device;
    PCIe_FB* framebuffer;

    aos_bool acceleration_present;

    // Function pointers
    aos_bool (*init)(struct AOS_Module* m);
    aos_bool (*init_resources)(struct gpu_device* gpu, int resource_id);
    aos_bool (*set_mode)(struct gpu_device* gpu, uint32_t width, uint32_t height, uint32_t bpp);
    aos_bool (*swap_buffers)(struct gpu_device* gpu);
    aos_bool (*flush)(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id);
    aos_bool (*switch_off)(struct gpu_device* gpu);

    // API pointers
    struct pyrion_api pyrion;

    aos_bool active;
} gpu_device_t;

void svga_write(uintptr_t mmio_base, uint32_t index, uint32_t value, uint32_t svga_index_port, uint32_t svga_value_port) __attribute__((used));
uint32_t svga_read(uintptr_t mmio_base, uint32_t index, uint32_t svga_index_port, uint32_t svga_value_port) __attribute__((used));

aos_bool gpu_find_gpu(PCIe_FB* fb, struct AOS_Module** out) __attribute__((used));