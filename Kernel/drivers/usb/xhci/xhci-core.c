#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/core/module.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/usb/usb.h>
#include <inc/drivers/usb/xhci/xhci.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>
#include <inc/core/kfuncs.h>

#define DOORBELL_BASE 0x20

#define TRB_TRANSFER_EVENT 32
#define TRB_COMPLETION_EVENT 33
#define TRB_PORT_STATUS_CHANGE_EVENT 34
#define TRB_BANDWIDTH_REQ_EVENT 35
#define TRB_DOORBELL_EVENT 36
#define TRB_HOST_CONTROLLER_EVENT 37
#define TRB_DEVICE_NOTIFICATION_EVENT 38
#define TRB_MFINDEX_WRAP_EVENT 39

#define TRB_CMD_NOOP 23
#define TRB_CMD_ENABLE_SLOT 9
#define TRB_CMD_DISABLE_SLOT 10
#define TRB_CMD_ADDRESS_DEVICE 11
#define TRB_CMD_CONFIGURE_ENDPOINT 12
#define TRB_CMD_EVALUATE_CONTEXT 13
#define TRB_CMD_RESET_ENDPOINT 14
#define TRB_CMD_STOP_ENDPOINT 15
#define TRB_CMD_SET_TR_DEQUEUE 16
#define TRB_CMD_RESET_DEVICE 17
#define TRB_CMD_FORCE_EVENT 18
#define TRB_CMD_NEGOTIATE_BW 19
#define TRB_CMD_SET_LATENCY_TOL 20
#define TRB_CMD_GET_PORT_BANDWIDTH 21
#define TRB_CMD_FORCE_HEADER 22

#define KxHCI_ALLOC_STEP 16

typedef struct {
	uint64_t idx;
	aos_bool valid;

	pcie_device_t xhci_controller;
	volatile uint32_t* xhci_mmio;
	struct xhci_cap_regs* cap;
	struct xhci_op_regs* op_regs;
	volatile uint32_t* doorbells;
	struct xhci_runtime_regs* runtime_regs;

	uint64_t dcbaa;

	struct xhci_trb* cmd_ring;
	uint64_t cmd_ring_phys;
	uint32_t cmd_index;
	uint8_t cmd_cycle;

	struct xhci_trb* event_ring;
	uint32_t event_index;
	uint8_t event_cycle;

	struct xhci_erst_entry* erst;
	uint64_t erst_phys;
	uint64_t event_ring_phys;

	uint32_t max_slots;
	uint32_t max_ports;
	uint64_t mapping_size;
	uint64_t trbs_per_page;

	uint64_t port;
	uint64_t speed;
} xhci_controller;

static xhci_controller* controllers;
static uint64_t controller_count;
static uint64_t controller_cap;

static void destroy_n_unmap_xhci(xhci_controller* kxc) {
	if (!kxc) return;

	if (kxc->cmd_ring) { avmf_free((uint64_t)kxc->cmd_ring); kxc->cmd_ring = NULL; }
	if (kxc->event_ring) { avmf_free((uint64_t)kxc->event_ring); kxc->event_ring = NULL; }
	if (kxc->erst) { avmf_free((uint64_t)kxc->erst); kxc->erst = NULL; }
	if (kxc->dcbaa > 0) { avmf_free(kxc->dcbaa); kxc->dcbaa = 0; }
	
	if (kxc->idx == controller_count - 1) controller_count--;
	kxc->valid = AOS_FALSE;
}

static aos_bool map_xhci_mmio(xhci_controller* kxc) {
	if (!kxc) return AOS_FALSE;

    uint8_t bus = kxc->xhci_controller.bus;
    uint8_t slot = kxc->xhci_controller.slot;
    uint8_t func = kxc->xhci_controller.func;

    uint32_t bar0 = pcie_read_bar(bus, slot, func, 0);
    uint64_t bar_phys = bar0 & ~0xFULL;
	if ((bar_phys & 0xFFF) != 0) return AOS_FALSE; // BAR should be page-aligned

    aos_bool is_64bit = ((bar0 >> 1) & 0b011) == 0x2;
	uint32_t orig0 = bar0;
    uint32_t orig1 = 0;

	if ((bar0 & 0b001) == 0x1) {
		serial_print("[xHCI] BAR0 is not a memory BAR, mapping failed!\n");
		return AOS_FALSE;
	}

    if (is_64bit) {
        orig1 = pcie_read_bar(bus, slot, func, 1);
        bar_phys |= ((uint64_t)orig1 << 32);
    }

	pcie_toggle_memory_space(bus, slot, func, AOS_FALSE);

    pcie_write_bar(bus, slot, func, 0, 0xFFFFFFFF);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read_bar(bus, slot, func, 0);
    uint32_t mask1 = is_64bit ? pcie_read_bar(bus, slot, func, 1) : 0;

    pcie_write_bar(bus, slot, func, 0, orig0);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, orig1);

	pcie_toggle_memory_space(bus, slot, func, AOS_TRUE);

    uint64_t size = 0;
	if (is_64bit) {
		uint64_t mask = (mask0 & ~0xFULL) | ((uint64_t)mask1 << 32);
		size = ~mask + 1;
	} else {
		uint32_t mask = (uint32_t)(mask0 & ~0xFULL);
		size = (uint32_t)(~mask + 1);
	}

    kxc->mapping_size = size;
	if (kxc->mapping_size < sizeof(struct xhci_cap_regs)) return AOS_FALSE;

	pager_map_range(AOS_DIRECT_MAP_BASE + bar_phys, bar_phys, kxc->mapping_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	if (((struct xhci_cap_regs*)(AOS_DIRECT_MAP_BASE + bar_phys))->hc_version <= 0) {
		serial_print("[xHCI] HCVersion Invalid!\n");
		return AOS_FALSE;
	}

	pcie_toggle_busmaster(bus, slot, func, AOS_TRUE);

    kxc->xhci_mmio = (volatile uint32_t*)(AOS_DIRECT_MAP_BASE + bar_phys);
    kxc->cap = (struct xhci_cap_regs*)kxc->xhci_mmio;
    kxc->op_regs = (struct xhci_op_regs*)((uint8_t*)kxc->xhci_mmio + kxc->cap->cap_length);
	kxc->runtime_regs = (struct xhci_runtime_regs*)((uint8_t*)kxc->xhci_mmio + kxc->cap->rtsoff);
	kxc->doorbells = (volatile uint32_t*)((uint8_t*)kxc->xhci_mmio + kxc->cap->dboff);

	kxc->max_slots = kxc->cap->hcs_params1 & 0xFF;
	kxc->max_ports = (kxc->cap->hcs_params1 >> 24) & 0xFF;
	kxc->trbs_per_page = PAGE_SIZE / sizeof(struct xhci_trb);

	if (kxc->cap->cap_length > kxc->mapping_size) {
		serial_print("[xHCI] Device Broken! (CAP Length)\n");
		return AOS_FALSE;
	}

	if (kxc->cap->dboff >= kxc->mapping_size) {
		serial_print("[xHCI] Device Broken! (DB Offset)\n");
		return AOS_FALSE;
	}

	if (kxc->cap->rtsoff >= kxc->mapping_size) {
		serial_print("[xHCI] Device Broken! (RTS Offset)\n");
		return AOS_FALSE;
	}

    serial_printf("[xHCI] Mapped all XHCI (Size: 0x%llx) (Version: %u)\n", kxc->mapping_size, kxc->cap->hc_version);
	return AOS_TRUE;
}

static struct xhci_trb* xhci_next_event(xhci_controller* kxc) {
	if (!kxc) return NULL;

    struct xhci_trb* trb = &kxc->event_ring[kxc->event_index];

    if ((trb->control & 1) != kxc->event_cycle) return NULL;
    struct xhci_trb* result = trb;

    kxc->event_index++;
    if (kxc->event_index == kxc->trbs_per_page) {
        kxc->event_index = 0;
        kxc->event_cycle ^= 1;
    }
    kxc->runtime_regs->intr_reg_set[0].erdp = (kxc->event_ring_phys + kxc->event_index * sizeof(struct xhci_trb)) | (1 << 3);
    return result;
}

static struct xhci_trb* xhci_wait_cmd(xhci_controller* kxc) {
	if (!kxc) return NULL;

    uint64_t timeout = kget_ms_passed();
    while (1) {
        struct xhci_trb* event = xhci_next_event(kxc);
        if (event) {
            uint32_t type = (event->control >> 10) & 0x3F;
            if (type == TRB_COMPLETION_EVENT) return event;
        }
        if (kget_ms_passed() - timeout >= 5000) return NULL;
    }
}

static void xhci_send_cmd(xhci_controller* kxc, uint32_t type, uint64_t param) {
	if (!kxc) return;

	kxc->cmd_ring[kxc->cmd_index].param = param;
	kxc->cmd_ring[kxc->cmd_index].status = 0;
	kxc->cmd_ring[kxc->cmd_index].control = (type << 10) | kxc->cmd_cycle;

	kxc->cmd_index++;
	if (kxc->cmd_index == kxc->trbs_per_page-1){
		kxc->cmd_index = 0;
		kxc->cmd_cycle ^= 1;
	}

	kxc->doorbells[0] = 0;
}

aos_bool xhci_init(struct AOS_Module* module) {
	if (!module) return AOS_FALSE;
    if (module->hdr.type != MODULE_TYPE_DRIVER) return AOS_FALSE;
    if (module->Modules.driver_module.type != MODULE_DRIVER_TYPE_xHCI) return AOS_FALSE;
    
	if (!controllers) {
		controllers = (xhci_controller*)avmf_alloc(sizeof(xhci_controller) * KxHCI_ALLOC_STEP, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!controllers) return AOS_FALSE;
		controller_cap = KxHCI_ALLOC_STEP;
		controller_count = 0;
	} else if (controller_count >= controller_cap) {
		xhci_controller* nptr = (xhci_controller*)avmf_alloc(sizeof(xhci_controller) * (controller_cap + KxHCI_ALLOC_STEP), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!nptr) return AOS_FALSE;
		memcpy(nptr, controllers, sizeof(xhci_controller)*controller_count);
		avmf_free((uint64_t)controllers);
		controllers = nptr;
		controller_cap += KxHCI_ALLOC_STEP;
	}

	xhci_controller* kxc = &controllers[controller_count];
	memset(kxc, 0, sizeof(xhci_controller));
	kxc->idx = controller_count;
	kxc->valid = AOS_TRUE;
	controller_count++;

    kxc->xhci_controller = module->Modules.driver_module.pcie_device;
    if (!map_xhci_mmio(kxc)) {
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}

	// Reset
	serial_print("[xHCI] Resetting Controller...\n");
	kxc->op_regs->usbcmd &= ~1;
	uint64_t timeout = kget_ms_passed();
	while (!(kxc->op_regs->usbsts & (1 << 0))) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}

	serial_print("[xHCI] Setting HCRST...\n");
	kxc->op_regs->usbcmd |= (1 << 1); // Set HCRST
	timeout = kget_ms_passed();
	while (kxc->op_regs->usbcmd & (1 << 1)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}
	timeout = kget_ms_passed();
	while (kxc->op_regs->usbsts & (1 << 11)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}

	serial_print("[xHCI] Allocating DCBAA...\n");
	kxc->dcbaa = avmf_alloc(ALIGN_UP((kxc->max_slots + 1) * sizeof(uint64_t), 64), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &kxc->op_regs->dcbaap);
	if (!kxc->dcbaa) {
		serial_print("[xHCI] Failed to allocate DCBAA\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}
	memset((void*)kxc->dcbaa, 0, (kxc->max_slots + 1) * sizeof(uint64_t));

	serial_print("[xHCI] Allocating CMD Ring...\n");
	kxc->cmd_ring = (struct xhci_trb*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &kxc->cmd_ring_phys);
	if (!kxc->cmd_ring) {
		serial_print("[xHCI] Failed to allocate CMD Ring\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}
	memset(kxc->cmd_ring, 0, PAGE_SIZE);

	kxc->cmd_ring[kxc->trbs_per_page-1].param = kxc->cmd_ring_phys;
	kxc->cmd_ring[kxc->trbs_per_page-1].control = 1 | (6 << 10) | (1 << 1); // Link TRB Type | Toggle Cycle

	serial_print("[xHCI] Allocating EVENT Ring...\n");
	kxc->event_ring = (struct xhci_trb*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &kxc->event_ring_phys);
	if (!kxc->event_ring) {
		serial_print("[xHCI] Failed to allocate EVENT Ring\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}
	memset(kxc->event_ring, 0, PAGE_SIZE);

	serial_print("[xHCI] Allocating ERST...\n");
	kxc->erst = (struct xhci_erst_entry*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &kxc->erst_phys);
	if (!kxc->erst) {
		serial_print("[xHCI] Failed to allocate ERST\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}
	memset(kxc->erst, 0, PAGE_SIZE);

	kxc->cmd_index = 0;
	kxc->cmd_cycle = 1;
	kxc->event_index = 0;
	kxc->event_cycle = 1;

	kxc->op_regs->crcr = kxc->cmd_ring_phys | kxc->cmd_cycle;

	kxc->erst[0].ring_segment_base = kxc->event_ring_phys;
	kxc->erst[0].ring_segment_size = kxc->trbs_per_page;

	kxc->runtime_regs->intr_reg_set[0].erstsz = 1;
	kxc->runtime_regs->intr_reg_set[0].erstba = kxc->erst_phys;
	kxc->runtime_regs->intr_reg_set[0].erdp = kxc->event_ring_phys;

	kxc->op_regs->config = kxc->max_slots;

	kxc->op_regs->usbcmd |= 1;
	timeout = kget_ms_passed();
	while (kxc->op_regs->usbsts & 1) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}

	kxc->runtime_regs->intr_reg_set[0].iman |= (1 << 1); // Enable interrupts
	kxc->op_regs->usbcmd |= (1 << 2); // Enable Global interrupts

	xhci_send_cmd(kxc, TRB_CMD_ENABLE_SLOT, 0);
	struct xhci_trb* event = xhci_wait_cmd(kxc);
	if (!event) {
		serial_print("[xHCI] Enable Slot timeout\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}

	if (((event->status >> 24) & 0xFF) != 1) {
		serial_printf("[xHCI] Enable Slot Command failed: %u\n", ((event->status >> 24) & 0xFF));
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}
	
	kxc->port = UINT64_MAX;
	uint8_t slot_id = (event->control >> 24) & 0xFF;
	for (uint32_t i = 0; i < kxc->max_ports; i++) {
		uint32_t portsc = kxc->op_regs->ports[i].portsc;
		if (portsc & 1) {
			serial_printf("[xHCI] Found Device on Port: %u\n", i + 1);
			kxc->port = i;
			break;
		}
	}

	if (kxc->port == UINT64_MAX) {
		serial_print("[xHCI] No devices found!\n");
		destroy_n_unmap_xhci(kxc);
		return AOS_FALSE;
	}

	serial_printf("[xHCI] Resetting Port %llu...\n", kxc->port);

	uint32_t portsc = kxc->op_regs->ports[kxc->port].portsc;
	portsc &= ~(0x7F << 17);
	portsc |= (1 << 4); // PR
	kxc->op_regs->ports[kxc->port].portsc = portsc;

	timeout = kget_ms_passed();
	while (kxc->op_regs->ports[kxc->port].portsc & (1 << 4)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Port reset timed out!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}
	timeout = kget_ms_passed();
	while (!(kxc->op_regs->ports[kxc->port].portsc & (1 << 1))) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Port Enable timed out!\n");
			destroy_n_unmap_xhci(kxc);
			return AOS_FALSE;
		}
		asm volatile("pause");
	}

	kxc->speed = (kxc->op_regs->ports[kxc->port].portsc >> 10) & 0xF;
	serial_printf("[xHCI] Port speed: %u\n", kxc->speed);

    return AOS_TRUE;
}
