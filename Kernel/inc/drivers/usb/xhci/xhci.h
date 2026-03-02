#pragma once

#include <inttypes.h>

#define XHCI_PROGRAMMIMG_INTERFACE 0x30 // xHCI

typedef struct xhci_cap_regs {
    uint8_t cap_length; // offset 0x00
    uint8_t reserved;
    uint16_t hc_version; // offset 0x02
    uint32_t hcs_params1; // offset 0x04
    uint32_t hcs_params2; // offset 0x08
    uint32_t hcs_params3; // offset 0x0C
    uint32_t hcc_params; // offset 0x10
    uint64_t doorbell_offset; // offset 0x14
} __attribute__((packed));

int xhci_init(void) __attribute__((used));