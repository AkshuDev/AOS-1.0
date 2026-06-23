#pragma once

#include <aos_inttypes.h>

#include <inc/core/module.h>

#define XHCI_PROGRAMMIMG_INTERFACE 0x30 // xHCI

struct xhci_cap_regs {
    uint8_t cap_length; // offset 0x00
    uint8_t reserved;
    uint16_t hc_version; // offset 0x02
    uint32_t hcs_params1; // offset 0x04
    uint32_t hcs_params2; // offset 0x08
    uint32_t hcs_params3; // offset 0x0C
    uint32_t hcc_params; // offset 0x10
    uint32_t dboff; // offset 0x14
    uint32_t rtsoff; // offset 0x18
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

uint8_t xhci_init(struct AOS_Module* module) __attribute__((used));