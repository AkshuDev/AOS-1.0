#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/core/module.h>

#include <inc/mm/pager.h>

#include <inc/drivers/usb/usb.h>
#include <inc/drivers/usb/xhci/xhci.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>

#define DOORBELL_BASE 0x20

static pcie_device_t xhci_controller = {0};

static volatile uint32_t* xhci_mmio = NULL;
static struct xhci_cap_regs* cap = NULL;
static volatile uint32_t* op_regs = NULL;

static struct xhci_trb* cmd_ring = NULL;
static uint32_t cmd_index = 0;
static uint8_t cmd_cycle = 1;

static void map_xhci_mmio(void) {
    uint8_t bus = xhci_controller.bus;
    uint8_t slot = xhci_controller.slot;
    uint8_t func = xhci_controller.func;

    uint32_t bar0 = pcie_read(bus, slot, func, 0x10);
    uint64_t bar_phys = bar0 & ~0xFULL;

    int is_64bit = ((bar0 >> 1) & 0x3) == 0x2;

    if (is_64bit) {
        uint32_t bar1 = pcie_read(bus, slot, func, 0x14);
        bar_phys |= ((uint64_t)bar1 << 32);
    }

    uint32_t orig0 = bar0;
    uint32_t orig1 = 0;

    if (is_64bit) {
        orig1 = pcie_read(bus, slot, func, 0x14);
    }

    pcie_write(bus, slot, func, 0x10, 0xFFFFFFFF);
    if (is_64bit)
        pcie_write(bus, slot, func, 0x14, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read(bus, slot, func, 0x10);
    uint32_t mask1 = is_64bit ? pcie_read(bus, slot, func, 0x14) : 0;

    pcie_write(bus, slot, func, 0x10, orig0);
    if (is_64bit)
        pcie_write(bus, slot, func, 0x14, orig1);

    uint64_t mask = mask0 & ~0xFULL;
    if (is_64bit)
        mask |= ((uint64_t)mask1 << 32);

    uint64_t size = ~(mask) + 1;

    pager_map_range(AOS_xHCI_VIRT_BASE, bar_phys, size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    xhci_mmio = (volatile uint32_t*)AOS_xHCI_VIRT_BASE;
    cap = (struct xhci_cap_regs*)xhci_mmio;
    op_regs = (volatile uint32_t*)((uint8_t*)xhci_mmio + cap->cap_length);

    serial_printf("Mapped all XHCI (Size: 0x%llx)\n", size);
}

static void xhci_send_cmd(uint32_t type, uint64_t param) {
    struct xhci_trb* trb = &cmd_ring[cmd_index++];

    trb->param = param;
    trb->status = 0;
    trb->control = (type << 10) | cmd_cycle;

    if (cmd_index >= 256) {
        cmd_index = 0;
        cmd_cycle ^= 1;
    }

    volatile uint32_t* doorbell = (volatile uint32_t*)(xhci_mmio + cap->dboff);
    doorbell[0] = 0;
}

uint8_t xhci_init(struct AOS_Module* module) {
    if (!module) return 0;
    if (module->hdr.type != MODULE_TYPE_DRIVER) return 0;
    if (module->Modules.driver_module.type != MODULE_DRIVER_TYPE_xHCI) return 0;
    
    xhci_controller = module->Modules.driver_module.pcie_device;
    map_xhci_mmio();

    return 1;
}
