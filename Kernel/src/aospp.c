#include <inttypes.h>
#include <system.h>
#include <asm.h>
#include <fonts.h>

#include <inc/core/acpi.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>
#include <inc/drivers/core/gpu.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

void aospp_start() __attribute__((used));
void aospp() __attribute__((used));

pcie_device_t pcie_gpu_device = {0};
gpu_device_t gpu_device = {0};
PCIe_FB gpu_framebuffer = {0};

static FB_Cursor_t fb_cur = {
    .x = 0,
    .y = 0,
    .fg_color = 0xFFFFFFFF,
    .bg_color = 0xFF000000
};

static struct page_table* kernel_pml4 = (struct page_table*)NULL;

void aospp_start() {
    serial_print("Searching for GPU...\n");
    (void)gpu_get_framebuffer_and_info(&gpu_framebuffer, &pcie_gpu_device, &gpu_device); // FB is useless, we query device directly, this is for legacy

    serial_print("Initializing GPU Driver...\n");
    if (gpu_device.init != NULL) gpu_device.init(&gpu_device);

    // Map framebuffer
    gpu_framebuffer.virt = avmf_alloc(gpu_framebuffer.size, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &(gpu_framebuffer.phys));

    if (gpu_device.init_resources != NULL) gpu_device.init_resources(&gpu_device);

    #define GPU_FLUSH gpu_device.flush(&gpu_device, 0, 0, gpu_device.framebuffer->w, gpu_device.framebuffer->h); 

    fb_init((uint32_t*)gpu_framebuffer.virt, gpu_framebuffer.w, gpu_framebuffer.h, gpu_framebuffer.pitch, 32);

    fb_clear(0x0000FFFF);
    if (gpu_device.flush != NULL) GPU_FLUSH;

//    fb_print(&fb_cur, "Hello this is AOS++ Shell!\n");
//    if (gpu_device.flush != NULL) GPU_FLUSH;
}
