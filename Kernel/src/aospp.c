#include <aos_inttypes.h>
#include <system.h>
#include <asm.h>
#include <fonts.h>

#include <inc/core/kfuncs.h>
#include <inc/core/module.h>
#include <inc/core/acpi.h>
#include <inc/core/pcie.h>
#include <inc/core/vshell.h>
#include <inc/core/smp.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/gpu/apis/pyrion.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

void aospp_start() __attribute__((used));
void aospp() __attribute__((used));

static struct AOS_Module* gpu_m;
static PCIe_FB gpu_framebuffer;
static struct pyrion_ctx* display_ctx;

void aos_start_vshell(void) {
    start_vshell(display_ctx);
    smp_yield();
}

void aospp_start(void) {
    serial_print("[AOS++] Searching for GPU...\n");
    if (!gpu_find_gpu(&gpu_framebuffer, &gpu_m)) {
        serial_print("[AOS++] Failed to find/detect any GPU!\n");
        return;
    }

	gpu_device_t* gpu_device = &gpu_m->Modules.driver_module.DriverConnections.gpu_connector;

    serial_print("[AOS++] Initializing GPU Driver...\n");
    if (gpu_device->init != NULL) gpu_device->init(gpu_m);

    if (gpu_device->init_resources != NULL) gpu_device->init_resources(gpu_device, 1);
    serial_print("[AOS++] Initializing Pyrion...\n");
    pyrion_init(gpu_device);
    serial_print("[AOS++] Creating main display Pyrion Context...\n");
    display_ctx = pyrion_create_ctx();
    if (display_ctx == NULL) return;

    start_vshell(display_ctx);

    for (;;) asm("hlt");
}

