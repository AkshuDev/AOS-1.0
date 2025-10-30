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
    avmf_init(0xF0000000, 64*1024*1024); // 64MB Reserve
    serial_print("AOS++ LOADED!\n");

    uint64_t fb_physaddr = gpu_get_framebuffer_and_info(&gpu_framebuffer, &pcie_gpu_device, &gpu_device);
    if (fb_physaddr == 0) {
        serial_print("Failed to get framebuffer\n");
        return;
    }
    gpu_device.init(&gpu_device);
    serial_printf("Framebuffer Physical addr: 0x%x\nGPU Vendor: %s\n", (uint32_t)fb_physaddr, gpu_device.name);
    fb_size = gpu_framebuffer.size;

    serial_printf("Framebuffer Size: %uX%u\n", gpu_framebuffer.w, gpu_framebuffer.h);

    fb_addr = avmf_alloc_region(fb_size, AVMF_FLAG_PRESENT | AVMF_FLAG_WRITEABLE);
    pager_init(0, 0);

    if (avmf_map(fb_addr, fb_physaddr, fb_size, AVMF_FLAG_PRESENT|AVMF_FLAG_WRITEABLE) != 0) {
        serial_print("Failed to map framebuffer!\n");
        return;
    }
    kernel_pml4 = pager_map(fb_addr, fb_physaddr, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    pager_load(kernel_pml4);
    serial_printf("Framebuffer Virtual addr: 0x%x\n", (uint32_t)fb_addr);

    fb_init((uint32_t*)&fb_addr, 1024, 768, 1024*4, 32);

    volatile uint32_t *fb_test = (volatile uint32_t*)(uintptr_t)fb_addr;
    fb_test[0] = 0x00FF00;

    serial_print("Clearing...\n");
    fb_clear(0x00FF00);

    serial_print("Welcome!\n");

    fb_print(&fb_cur, "Hello this is AOS++ Shell, More modern using Framebuffer!\n");
}
