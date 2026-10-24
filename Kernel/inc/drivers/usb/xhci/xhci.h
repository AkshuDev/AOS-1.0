#pragma once

#include <aos_inttypes.h>

#include <inc/core/module.h>

#define XHCI_PROGRAMMIMG_INTERFACE 0x30 // xHCI

struct xhci_cap_regs {
    uint32_t cap_length_res_hci_version; // offset 0x00 (HCI Version, Reserved, Cap Length)
    uint32_t hcs_params1; // offset 0x04
    uint32_t hcs_params2; // offset 0x08
    uint32_t hcs_params3; // offset 0x0C
    uint32_t hcc_params1; // offset 0x10
    uint32_t dboff; // offset 0x14
    uint32_t rtsoff; // offset 0x18
	uint32_t hcc_params2; // offset 0x1C
} __attribute__((packed));

struct xhci_op_regs {
	uint32_t usbcmd; // offset 0x00
	uint32_t usbsts; // offset 0x04
	uint32_t page_size; // offset 0x08
	uint32_t reserved[2];
	uint32_t dnctrl; // offset 0x14
	uint64_t crcr; // offset 0x18
	uint32_t reserved1[4];
	uint64_t dcbaap; // offset 0x30
	uint32_t config; // offset 0x38
	uint8_t reserved2[0x3C4];
	struct {
		uint32_t portsc; // offset +0x0
		uint32_t portpmsc; // offset +0x4
		uint32_t portli; // offset +0x8
		uint32_t reserved3; // offset +0xC
	} ports[]; // starts at 0x400
} __attribute__((packed));

struct xhci_runtime_regs {
	uint32_t mfindex; // offset 0x0
	uint32_t reserved[7];
	struct {
		uint32_t iman; // offset +0x0
		uint32_t imod; // offset +0x4
		uint32_t erstsz; // offset +0x8
		uint32_t reserved;
		uint64_t erstba; // offset +0x10
		uint64_t erdp; // offset +0x18
	} intr_reg_set[];
} __attribute__((packed));

struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

struct xhci_erst_entry {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed));

enum xhci_port_flags {
	XHCI_PORT_FLAG_CCS = (1 << 0), // Current Connect Status
	XHCI_PORT_FLAG_PED = (1 << 1), // Port Enabled/Disabled
	XHCI_PORT_FLAG_OCA = (1 << 3), // Over-Current Active
	XHCI_PORT_FLAG_PR = (1 << 4), // Port Reset
	XHCI_PORT_FLAG_PLS = (1 << 5), // Port Link State (4 bits)
	XHCI_PORT_FLAG_PP = (1 << 9), // Port Power
	XHCI_PORT_FLAG_PS = (1 << 10), // Port Speed (4 bits)
	XHCI_PORT_FLAG_PIC = (1 << 14), // Port Indicator Control (2 bits)
	XHCI_PORT_FLAG_LWS = (1 << 16), // Port Link State Write Strobe
	XHCI_PORT_FLAG_CSC = (1 << 17), // Connect Status Change
	XHCI_PORT_FLAG_PEC = (1 << 18), // Port Enabled/Disabled Change
	XHCI_PORT_FLAG_WRS = (1 << 19), // Warm Port Reset Change
	XHCI_PORT_FLAG_OCC = (1 << 20), // Over-Current Change
	XHCI_PORT_FLAG_PRC = (1 << 21) // Port Reset Change
};

enum xhci_ubsts_flags {
	XHCI_UBSTS_FLAG_HCH = (1 << 0), // HCHalted
	XHCI_UBSTS_FLAG_HSE = (1 << 2), // Host System Error
	XHCI_UBSTS_FLAG_EINT = (1 << 3), // Event Interrupt
	XHCI_UBSTS_FLAG_PCD = (1 << 4), // Port Change Detected
	XHCI_UBSTS_FLAG_SSS = (1 << 8), // Save State Status
	XHCI_UBSTS_FLAG_RSS = (1 << 9), // Restore State Status
	XHCI_UBSTS_FLAG_SRE = (1 << 10), // Save/Restore Error
	XHCI_UBSTS_FLAG_CNR = (1 << 11), // Controller Not Ready
	XHCI_UBSTS_FLAG_HCE = (1 << 12) // Host Controller Error
};

enum xhci_usbcmd_flags {
	XHCI_USBCMD_FLAG_RS = (1 << 0), // Run/Stop
	XHCI_USBCMD_FLAG_HCRST = (1 << 1), // Host Controller Reset
	XHCI_USBCMD_FLAG_INTE = (1 << 2), // Interrupter Enable
	XHCI_USBCMD_FLAG_HSEE = (1 << 3), // Host System Error Enable
	XHCI_USBCMD_FLAG_LHCRST = (1 << 7), // Light Host Controller Reset
	XHCI_USBCMD_FLAG_CSS = (1 << 8), // Controller Save State
	XHCI_USBCMD_FLAG_CRS = (1 << 9), // Controller Restore State
	XHCI_USBCMD_FLAG_EWE = (1 << 10), // Enable Wrap Event
	XHCI_USBCMD_FLAG_EU3S = (1 << 11), // Enable U3 MFINDEX Stop
	XHCI_USBCMD_FLAG_CME = (1 << 13), // CEM Enable
	XHCI_USBCMD_FLAG_ETE = (1 << 14), // Extended TBC Enable
	XHCI_USBCMD_FLAG_TSC_EN = (1 << 15), // Extended TBC TRB Status Enable
	XHCI_USBCMD_FLAG_VTIOE = (1 << 16) // VTIO Enable
};

enum xhci_crcr_flags {
	XHCI_CRCR_FLAG_RCS = (1 << 0), // Ring Cycle Status
	XHCI_CRCR_FLAG_CS = (1 << 1), // Command Stop
	XHCI_CRCR_FLAG_CA = (1 << 2), // Command Abort
	XHCI_CRCR_FLAG_CRR = (1 << 2) // Command Ring Running
};

enum xhci_command_trb_ids {
	XHCI_CMD_TRB_ID_ENABLE_SLOT = 9,
	XHCI_CMD_TRB_ID_DISABLE_SLOT = 10,
	XHCI_CMD_TRB_ID_ADDRESS_DEVICE = 11,
	XHCI_CMD_TRB_ID_CONFIGURE_ENDPOINT = 12,
	XHCI_CMD_TRB_ID_EVALUATE_CONTEXT = 13,
	XHCI_CMD_TRB_ID_RESET_ENDPOINT = 14,
	XHCI_CMD_TRB_ID_STOP_ENDPOINT = 15,
	XHCI_CMD_TRB_ID_SET_TR_DEQUEUE_PTR = 16,
	XHCI_CMD_TRB_ID_RESET_DEVICE = 17,
	XHCI_CMD_TRB_ID_FORCE_EVENT = 18,
	XHCI_CMD_TRB_ID_NEGOTIATE_BANDWIDTH = 19,
	XHCI_CMD_TRB_ID_SET_LATENCY_TOLERANCE_VALUE = 20,
	XHCI_CMD_TRB_ID_GET_PORT_BANDWIDTH = 21,
	XHCI_CMD_TRB_ID_FORCE_HEADER = 22,
	XHCI_CMD_TRB_ID_NO_OP = 23,
	XHCI_CMD_TRB_ID_GET_EXTENDED_PROPERTY = 24,
	XHCI_CMD_TRB_ID_SET_EXTENDED_PROPERTY = 25
};

enum xhci_event_trb_ids {
	XHCI_EVENT_TRB_ID_TRANSFER = 32,
	XHCI_EVENT_TRB_ID_COMMAND_COMPLETION = 33,
	XHCI_EVENT_TRB_ID_PORT_STATUS_CHANGE = 34,
	XHCI_EVENT_TRB_ID_BANDWIDTH_REQUEST = 35,
	XHCI_EVENT_TRB_ID_DOORBELL = 36,
	XHCI_EVENT_TRB_ID_HOST_CONTROLLER = 37,
	XHCI_EVENT_TRB_ID_DEVICE_NOTIFICATION = 38,
	XHCI_EVENT_TRB_ID_MFINDEX_WRAP = 39
};

aos_bool xhci_init(struct AOS_Module* module);