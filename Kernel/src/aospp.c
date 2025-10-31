#include <inttypes.h>
#include <system.h>
#include <asm.h>
#include <fonts.h>

#include <inc/acpi.h>
#include <inc/framebuffer.h>
#include <inc/io.h>
#include <inc/pcie.h>
#include <inc/gpu.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

void aospp_start() __attribute__((used));
void aospp() __attribute__((used));

pcie_device_t pcie_gpu_device = {0};
gpu_device_t gpu_device = {0};
PCIe_FB gpu_framebuffer = {0};
static uint64_t fb_addr = 0;
static uint64_t fb_size = 0;

static FB_Cursor_t fb_cur = {
    .x = 0,
    .y = 0,
    .fg_color = 0xFFFFFF,
    .bg_color = 0x000000
};

static struct page_table* kernel_pml4 = (struct page_table*)NULL;

void aospp_start() {
    serial_init();

    // Reserve MMIO region at 0xF0000000, kernel starts at 0x100000
    avmf_init(0x100000, 64*1024*1024); // reserve memory
    pager_init(0, 0); // init paging
    serial_print("AOS++ LOADED!\n");

    // Identity map GPU MMIO
    for (uint64_t offset = 0; offset < 64*1024*1024; offset += PAGE_SIZE) {
        pager_map(0xF0000000 + offset, 0xF0000000 + offset, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    }

    serial_print("Searching for GPU...\n");
    uint64_t fb_physaddr = gpu_get_framebuffer_and_info(&gpu_framebuffer, &pcie_gpu_device, &gpu_device);
    serial_print("Got Framebuffer!\n");

    if (fb_physaddr == 0) {
        serial_print("Failed to get framebuffer\n");
        return;
    }

    serial_print("Initializing GPU Driver...\n");
    if (gpu_device.init != NULL) gpu_device.init(&gpu_device);

    #define GPU_FLUSH gpu_device.flush(&gpu_device, 0, 0, gpu_device.framebuffer->w, gpu_device.framebuffer->h);

    fb_size = gpu_framebuffer.size;

    // Map framebuffer to high-half virtual address
    fb_addr = 0xFFFF800010000000ULL;
    for (uint64_t i = 0; i < fb_size; i += PAGE_SIZE) {
        pager_map(fb_addr + i, fb_physaddr + i, PAGE_PRESENT | PAGE_RW);
    }

    fb_init((uint32_t*)fb_addr, gpu_framebuffer.w, gpu_framebuffer.h,
            gpu_framebuffer.pitch, 32);

    fb_clear(0x00FF00);
    if (gpu_device.flush != NULL) GPU_FLUSH;
    
    fb_print(&fb_cur, "Hello this is AOS++ Shell!\n");
    if (gpu_device.flush != NULL) GPU_FLUSH;
}
