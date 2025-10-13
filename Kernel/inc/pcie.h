#pragma once
#include <inttypes.h>

#define PCI_MAX_BUS 256
#define PCI_MAX_SLOT 32
#define PCI_MAX_FUNC 8

#define PCI_CLASS_DISPLAY 0x03
#define PCI_SUBCLASS_VGA  0x00

#define PCI_BAR0 0x10
#define PCI_BAR_COUNT 6

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus, slot, func;
    uint32_t bar0;
} pci_device_t;

typedef struct {
    uint64_t fb_phys;
    uint32_t fb_w;
    uint32_t fb_h;
    uint32_t fb_pitch;
    uint8_t fb_bpp;
} PCIe_FB;

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) __attribute__((used));
int pci_scan(pci_device_t* dev) __attribute__((used));
int pcie_find_vga(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) __attribute__((used));
uint64_t pcie_get_vbe_framebuffer(PCIe_FB* fb) __attribute__((used));
