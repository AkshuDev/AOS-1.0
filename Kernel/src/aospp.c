#include <inttypes.h>
#include <system.h>
#include <asm.h>
#include <fonts.h>

#include <inc/core/acpi.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>
#include <inc/drivers/core/gpu.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/core/vshell.h>
#include <inc/core/smp.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

void aospp_start() __attribute__((used));
void aospp() __attribute__((used));

static pcie_device_t pcie_gpu_device = {0};
static gpu_device_t gpu_device = {0};
static PCIe_FB gpu_framebuffer = {0};
static struct pyrion_ctx* display_ctx = NULL;

void aos_start_vshell(void) {
    start_vshell(display_ctx);
    smp_yield();
}

void aospp_start(void) {
    serial_print("Searching for GPU...\n");
    (void)gpu_get_framebuffer_and_info(&gpu_framebuffer, &pcie_gpu_device, &gpu_device); // FB is useless, we query device directly, this is for legacy

    serial_print("Initializing GPU Driver...\n");
    if (gpu_device.init != NULL) gpu_device.init(&gpu_device);

    // Map framebuffer
    gpu_framebuffer.virt = avmf_alloc(gpu_framebuffer.size, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW | PAGE_PCD, &(gpu_framebuffer.phys));

    if (gpu_device.init_resources != NULL) gpu_device.init_resources(&gpu_device, 1);
    serial_print("Initializing Pyrion...\n");
    pyrion_init(&gpu_device);
    serial_print("Creating main display Pyrion Context...\n");
    display_ctx = pyrion_create_ctx();
    if (display_ctx == NULL) return;

    smp_push_task(1, aos_start_vshell);

    for (;;) asm("hlt");
}

