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

int pcie_find_sata(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) {
    for (uint8_t b = 0; b < PCI_MAX_BUS; b++) {
        for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
            for (uint8_t f = 0; f < PCI_MAX_FUNC; f++) {
                uint32_t data = pcie_read(b, s, f, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                
                uint32_t class_data = pcie_read(b, s, f, 0x08);
                uint8_t class = (class_data >> 24) & 0xFF;
                uint8_t subclass = (class_data >> 16) & 0xFF;
                
                if (class == PCI_CLASS_MASS_STORAGE && subclass == PCI_SUBCLASS_AHCI) { // SATA (AHCI)
                    *bus = b;
                    *slot = s;
                    *func = f;
                    *bar0 = pcie_read(b, s, f, 0x10); // MMIO base
                    return 1;
                }
            }
        }
    }
    return 0;
}

int pcie_find_nvme(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) {
    for (uint8_t b = 0; b < PCI_MAX_BUS; b++) {
        for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
            for (uint8_t f = 0; f < PCI_MAX_FUNC; f++) {
                uint32_t data = pcie_read(b, s, f, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                
                uint32_t class_data = pcie_read(b, s, f, 0x08);
                uint8_t class = (class_data >> 24) & 0xFF;
                uint8_t subclass = (class_data >> 16) & 0xFF;
                
                if (class == PCI_CLASS_MASS_STORAGE && subclass == PCI_SUBCLASS_NVme) { // NVMe
                    *bus = b;
                    *slot = s;
                    *func = f;
                    *bar0 = pcie_read(b, s, f, 0x10); // MMIO base
                    return 1;
                }
            }
        }
    }
    return 0;
}


int pcie_find_vga(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0) {
    for (uint8_t b = 0; b < PCI_MAX_BUS; b++) {
        for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
            for (uint8_t f = 0; f < PCI_MAX_FUNC; f++) {
                uint32_t data = pcie_read(b, s, f, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint32_t class_data = pcie_read(b, s, f, 0x08);
                uint8_t class = (class_data >> 24) & 0xFF;
                if (class == PCI_VGA_DISPLAY) { // VGA controller
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

