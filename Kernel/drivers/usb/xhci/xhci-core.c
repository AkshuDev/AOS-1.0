#include <inttypes.h>
#include <asm.h>

#include <inc/drivers/usb/usb.h>
#include <inc/drivers/usb/xhci/xhci.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>

static pcie_device_t xhci_controller = {0};
static uint8_t found_xhci = 0;

static volatile uint32_t* xhci_mmio = NULL;
static struct xhci_cap_regs* cap = NULL;
static volatile uint32_t* op_regs = NULL;

static void map_xhci_mmio(void) {
    if (!found_xhci) return;
    xhci_mmio = (volatile uint32_t*)xhci_controller.bar0;
    cap = (struct xhci_cap_regs*)xhci_mmio;
    op_regs = xhci_mmio + cap->cap_length / 4;

    serial_printf("Mapped all XHCI Info!\n");
}

static void find_xhci(void) {
    if (found_xhci == 1) return;
    int out = pcie_find_ex(&xhci_controller.bus, &xhci_controller.slot, &xhci_controller.func, (uint32_t*)&xhci_controller.bar0, SERIAL_BUS_CONTROLLER_CLASS, USB_SUB_CLASS, XHCI_PROGRAMMIMG_INTERFACE, 0, 0);
    serial_printf("Found XHCI: %d\n", out);
    if (out != 1) {
        found_xhci = 0; // ensure
        return;
    }
    found_xhci = 1;

    map_xhci_mmio();
}

int xhci_init(void) {
    find_xhci();
    if (!found_xhci) return 1;

    uint16_t hc_version = cap->hc_version;
    serial_printf("xHCI Controller found: version %x\n", hc_version);
}
