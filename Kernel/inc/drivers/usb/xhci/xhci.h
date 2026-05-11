#pragma once

#include <inttypes.h>

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

struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

uint8_t xhci_init(struct AOS_Module* module) __attribute__((used));