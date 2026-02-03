#pragma once
#include <inttypes.h>

#define PCI_MAX_BUS 256
#define PCI_MAX_SLOT 32
#define PCI_MAX_FUNC 8

#define PCI_CLASS_DISPLAY 0x12
#define PCI_VGA_DISPLAY 0x03
#define PCI_SUBCLASS_VGA  0x00

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_AHCI 0x06
#define PCI_SUBCLASS_NVMe 0x08

#define PCI_BAR0 0x10
#define PCI_BAR_COUNT 6

#define PCIE_VIRT_BASE 0xFFFFC00000000000ULL

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus, slot, func;
    uint64_t bar0;
} pcie_device_t;

typedef struct {
    uint64_t phys;
    uint64_t virt;
    uint64_t mmio_base;
    uint32_t w;
    uint32_t h;
    uint32_t pitch;
    uint8_t bpp;
    uint32_t size;
} PCIe_FB;

uint8_t pcie_init() __attribute__((used));
uint32_t pcie_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) __attribute__((used));
uint32_t pcie_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) __attribute__((used));
int pcie_find_nvme(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) __attribute__((used));
int pcie_find_sata(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) __attribute__((used));
int pcie_find_vga(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) __attribute__((used));
