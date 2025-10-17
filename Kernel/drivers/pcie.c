#include <asm.h>
#include <inttypes.h>

#include <inc/pcie.h>

uint32_t pcie_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    return pcie_read(bus, slot, func, PCI_BAR0 + (bar_index * 4));
}

uint32_t pcie_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    asm_outl(0xCF8, addr);
    return asm_inl(0xCFC);
}

int pcie_scan(pcie_device_t* dev) {
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t data = pcie_read(bus, slot, func, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint16_t device = (data >> 16) & 0xFFFF;
                uint32_t classcode_data = pcie_read(bus, slot, func, 0x08);
                uint8_t class = (classcode_data >> 24) & 0xFF;
                if (class == 0x03) { // VGA
                    dev->vendor_id = vendor;
                    dev->device_id = device;
                    dev->class_code = class;
                    dev->bus = bus;
                    dev->slot = slot;
                    dev->func = func;
                    dev->bar0 = pcie_read(bus, slot, func, 0x10); // MMIO base
                    return 1; // found
                }
            }
        }
    }
    return 0; // none found
}

int pcie_find_vga(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) {
    for (uint8_t b = 0; b < 256; b++) {
        for (uint8_t s = 0; s < 32; s++) {
            for (uint8_t f = 0; f < 8; f++) {
                uint32_t data = pcie_read(b, s, f, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint32_t class_data = pcie_read(b, s, f, 0x08);
                uint8_t class = (class_data >> 24) & 0xFF;
                if (class == 0x03) { // VGA controller
                    *bus = b;
                    *slot = s;
                    *func = f;
                    *bar0 = pcie_read(b, s, f, 0x10);
                    return 1;
                }
            }
        }
    }
    return 0;
}

