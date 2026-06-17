#include <inttypes.h>
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

static pcie_device_t xhci_controller = {0};

static volatile uint32_t* xhci_mmio;
static struct xhci_cap_regs* cap;
static struct xhci_op_regs* op_regs;
static volatile uint32_t* doorbells;
static struct xhci_runtime_regs* runtime_regs;

static uint64_t dcbaa;

static struct xhci_trb* cmd_ring;
static uint64_t cmd_ring_phys;
static uint32_t cmd_index;
static uint8_t cmd_cycle;

static struct xhci_trb* event_ring;
static uint32_t event_index;
static uint8_t event_cycle;

static struct xhci_erst_entry* erst;
static uint64_t erst_phys;
static uint64_t event_ring_phys;

static uint32_t max_slots;
static uint32_t max_ports;
static uint64_t mapping_size;
static uint64_t trbs_per_page;

static uint64_t port;
static uint64_t speed;

static void destroy_n_unmap_xhci() {
	if (cmd_ring) { avmf_free((uint64_t)cmd_ring); cmd_ring = NULL; }
	if (event_ring) { avmf_free((uint64_t)event_ring); event_ring = NULL; }
	if (erst) { avmf_free((uint64_t)erst); erst = NULL; }
	if (dcbaa > 0) { avmf_free(dcbaa); dcbaa = 0; }

	for (uint64_t i = 0; i < mapping_size / PAGE_SIZE; i++) pager_unmap(AOS_xHCI_VIRT_BASE + (i * PAGE_SIZE));
}

static uint8_t map_xhci_mmio(void) {
    uint8_t bus = xhci_controller.bus;
    uint8_t slot = xhci_controller.slot;
    uint8_t func = xhci_controller.func;

    uint32_t bar0 = pcie_read_bar(bus, slot, func, 0);
    uint64_t bar_phys = bar0 & ~0xFULL;

    uint8_t is_64bit = ((bar0 >> 1) & 0b011) == 0x2;
	uint32_t orig0 = bar0;
    uint32_t orig1 = 0;

	if ((bar0 & 0b001) == 0x1) {
		serial_print("[xHCI] BAR0 is not a memory BAR, mapping failed!\n");
		return 0;
	}

    if (is_64bit) {
        orig1 = pcie_read_bar(bus, slot, func, 1);
        bar_phys |= ((uint64_t)orig1 << 32);
    }

    pcie_write_bar(bus, slot, func, 0, 0xFFFFFFFF);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read_bar(bus, slot, func, 0);
    uint32_t mask1 = is_64bit ? pcie_read_bar(bus, slot, func, 1) : 0;

    pcie_write_bar(bus, slot, func, 0, orig0);
    if (is_64bit) pcie_write_bar(bus, slot, func, 1, orig1);

    uint64_t mask = mask0 & ~0xFULL;
    if (is_64bit) mask |= ((uint64_t)mask1 << 32);

    mapping_size = ~(mask) + 1;

    pager_map_range(AOS_xHCI_VIRT_BASE, bar_phys, mapping_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	pcie_enable_busmaster(bus, slot, func);

    xhci_mmio = (volatile uint32_t*)AOS_xHCI_VIRT_BASE;
    cap = (struct xhci_cap_regs*)xhci_mmio;
    op_regs = (struct xhci_op_regs*)((uint8_t*)xhci_mmio + cap->cap_length);
	runtime_regs = (struct xhci_runtime_regs*)((uint8_t*)xhci_mmio + cap->rtsoff);
	doorbells = (volatile uint32_t*)((uint8_t*)xhci_mmio + cap->dboff);

	max_slots = cap->hcs_params1 & 0xFF;
	max_ports = (cap->hcs_params1 >> 24) & 0xFF;
	trbs_per_page = PAGE_SIZE / sizeof(struct xhci_trb);

    serial_printf("[xHCI] Mapped all XHCI (Size: 0x%llx) (Version: %u)\n", mapping_size, cap->hc_version);
	return 1;
}

static struct xhci_trb* xhci_next_event(void) {
    struct xhci_trb* trb = &event_ring[event_index];

    if ((trb->control & 1) != event_cycle) return NULL;
    struct xhci_trb* result = trb;

    event_index++;
    if (event_index == trbs_per_page) {
        event_index = 0;
        event_cycle ^= 1;
    }
    runtime_regs->intr_reg_set[0].erdp = (event_ring_phys + event_index * sizeof(struct xhci_trb)) | (1 << 3);
    return result;
}

static struct xhci_trb* xhci_wait_cmd(void) {
    uint64_t timeout = kget_ms_passed();
    while (1) {
        struct xhci_trb* event = xhci_next_event();
        if (event) {
            uint32_t type = (event->control >> 10) & 0x3F;
            if (type == TRB_COMPLETION_EVENT) return event;
        }
        if (kget_ms_passed() - timeout >= 5000) return NULL;
    }
}

static void xhci_send_cmd(uint32_t type, uint64_t param) {
	cmd_ring[cmd_index].param = param;
	cmd_ring[cmd_index].status = 0;
	cmd_ring[cmd_index].control = (type << 10) | cmd_cycle;

	cmd_index++;
	if (cmd_index == trbs_per_page-1){
		cmd_index = 0;
		cmd_cycle ^= 1;
	}

	doorbells[0] = 0;
}

uint8_t xhci_init(struct AOS_Module* module) {
    if (!module) return 0;
    if (module->hdr.type != MODULE_TYPE_DRIVER) return 0;
    if (module->Modules.driver_module.type != MODULE_DRIVER_TYPE_xHCI) return 0;
    
    xhci_controller = module->Modules.driver_module.pcie_device;
    if (!map_xhci_mmio()) return 0;

	// Reset
	op_regs->usbcmd &= ~1;
	uint64_t timeout = kget_ms_passed();
	while (!(op_regs->usbsts & (1 << 0))) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			for (uint64_t i = 0; i < mapping_size / PAGE_SIZE; i++) pager_unmap(AOS_xHCI_VIRT_BASE + (i * PAGE_SIZE));
			return 0;
		}
	}
	op_regs->usbcmd |= (1 << 1); // Set HCRST
	timeout = kget_ms_passed();
	while (op_regs->usbcmd & (1 << 1)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			for (uint64_t i = 0; i < mapping_size / PAGE_SIZE; i++) pager_unmap(AOS_xHCI_VIRT_BASE + (i * PAGE_SIZE));
			return 0;
		}
	}
	timeout = kget_ms_passed();
	while (op_regs->usbsts & (1 << 11)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			for (uint64_t i = 0; i < mapping_size / PAGE_SIZE; i++) pager_unmap(AOS_xHCI_VIRT_BASE + (i * PAGE_SIZE));
			return 0;
		}
	}

	dcbaa = avmf_alloc(ALIGN_UP((max_slots + 1) * sizeof(uint64_t), 64), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &op_regs->dcbaap);
	if (!dcbaa) {
		serial_print("[xHCI] Failed to allocate DCBAA\n");
		for (uint64_t i = 0; i < mapping_size / PAGE_SIZE; i++) pager_unmap(AOS_xHCI_VIRT_BASE + (i * PAGE_SIZE));
		return 0;
	}
	memset((void*)dcbaa, 0, (max_slots + 1) * sizeof(uint64_t));

	cmd_ring = (struct xhci_trb*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &cmd_ring_phys);
	if (!cmd_ring) {
		serial_print("[xHCI] Failed to allocate CMD Ring\n");
		destroy_n_unmap_xhci();
		return 0;
	}
	memset(cmd_ring, 0, PAGE_SIZE);

	cmd_ring[trbs_per_page-1].param = cmd_ring_phys;
	cmd_ring[trbs_per_page-1].control = 1 | (6 << 10) | (1 << 1); // Link TRB Type | Toggle Cycle

	event_ring = (struct xhci_trb*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &event_ring_phys);
	if (!event_ring) {
		serial_print("[xHCI] Failed to allocate EVENT Ring\n");
		destroy_n_unmap_xhci();
		return 0;
	}
	memset(event_ring, 0, PAGE_SIZE);

	erst = (struct xhci_erst_entry*)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &erst_phys);
	if (!erst) {
		serial_print("[xHCI] Failed to allocate ERST\n");
		destroy_n_unmap_xhci();
		return 0;
	}
	memset(erst, 0, PAGE_SIZE);

	cmd_index = 0;
	cmd_cycle = 1;
	event_index = 0;
	event_cycle = 1;

	op_regs->crcr = cmd_ring_phys | cmd_cycle;

	erst[0].ring_segment_base = event_ring_phys;
	erst[0].ring_segment_size = trbs_per_page;

	runtime_regs->intr_reg_set[0].erstsz = 1;
	runtime_regs->intr_reg_set[0].erstba = erst_phys;
	runtime_regs->intr_reg_set[0].erdp = event_ring_phys;

	op_regs->config = max_slots;

	op_regs->usbcmd |= 1;
	timeout = kget_ms_passed();
	while (op_regs->usbsts & 1) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Controller Timeout!\n");
			destroy_n_unmap_xhci();
			return 0;
		}
	}

	runtime_regs->intr_reg_set[0].iman |= (1 << 1); // Enable interrupts
	op_regs->usbcmd |= (1 << 2); // Enable Global interrupts

	xhci_send_cmd(TRB_CMD_ENABLE_SLOT, 0);
	struct xhci_trb* event = xhci_wait_cmd();
	if (!event) {
		serial_print("[xHCI] Enable Slot timeout\n");
		destroy_n_unmap_xhci();
		return 0;
	}

	if (((event->status >> 24) & 0xFF) != 1) {
		serial_printf("[xHCI] Enable Slot Command failed: %u\n", ((event->status >> 24) & 0xFF));
		destroy_n_unmap_xhci();
		return 0;
	}
	
	port = UINT64_MAX;
	uint8_t slot_id = (event->control >> 24) & 0xFF;
	for (uint32_t i = 0; i < max_ports; i++) {
		uint32_t portsc = op_regs->ports[i].portsc;
		if (portsc & 1) {
			serial_printf("[xHCI] Found Device on Port: %u\n", i + 1);
			port = i;
			break;
		}
	}

	if (port == UINT64_MAX) {
		serial_print("[xHCI] No devices found!\n");
		destroy_n_unmap_xhci();
		return 0;
	}

	uint32_t portsc = op_regs->ports[port].portsc;
	portsc &= ~(0x7F << 17);
	portsc |= (1 << 4); // PR
	op_regs->ports[port].portsc = portsc;

	timeout = kget_ms_passed();
	while (op_regs->ports[port].portsc & (1 << 4)) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Port reset timed out!\n");
			destroy_n_unmap_xhci();
			return 0;
		}
	}
	timeout = kget_ms_passed();
	while (!(op_regs->ports[port].portsc & (1 << 1))) {
		if (kget_ms_passed() - timeout >= 10000) {
			serial_print("[xHCI] Port Enable timed out!\n");
			destroy_n_unmap_xhci();
			return 0;
		}
	}

	speed = (op_regs->ports[port].portsc >> 10) & 0xF;
	serial_printf("[xHCI] Port speed: %u\n", speed);

    return 1;
}
