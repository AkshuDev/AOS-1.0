#include <asm.h>
#include <inttypes.h>

#include <inc/pcie.h>

static uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    return pci_read(bus, slot, func, PCI_BAR0 + (bar_index * 4));
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    asm_outl(0xCF8, addr);
    return asm_inl(0xCFC);
}

int pci_scan(pci_device_t* dev) {
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t data = pci_read(bus, slot, func, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint16_t device = (data >> 16) & 0xFFFF;
                uint32_t classcode_data = pci_read(bus, slot, func, 0x08);
                uint8_t class = (classcode_data >> 24) & 0xFF;
                if (class == 0x03) { // VGA
                    dev->vendor_id = vendor;
                    dev->device_id = device;
                    dev->class_code = class;
                    dev->bus = bus;
                    dev->slot = slot;
                    dev->func = func;
                    dev->bar0 = pci_read(bus, slot, func, 0x10); // MMIO base
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
                uint32_t data = pci_read(b, s, f, 0);
                uint16_t vendor = data & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                uint32_t class_data = pci_read(b, s, f, 0x08);
                uint8_t class = (class_data >> 24) & 0xFF;
                if (class == 0x03) { // VGA controller
                    *bus = b;
                    *slot = s;
                    *func = f;
                    *bar0 = pci_read(b, s, f, 0x10);
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint32_t pcie_get_vbe_framebuffer(void) {
    uint8_t bus, slot, func;
    uint32_t bar_value = 0;

    for (bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (slot = 0; slot < PCI_MAX_SLOT; slot++) {
            for (func = 0; func < PCI_MAX_FUNC; func++) {
                uint32_t data = pci_read(bus, slot, func, 0);
                if (data == 0xFFFFFFFF) continue;

                uint16_t vendor = data & 0xFFFF;
                uint16_t device = (data >> 16) & 0xFFFF;
                if (vendor == 0xFFFF) continue;

                uint32_t class_data = pci_read(bus, slot, func, 0x08);
                uint8_t class_code = (class_data >> 24) & 0xFF;
                uint8_t subclass   = (class_data >> 16) & 0xFF;

                if (class_code == PCI_CLASS_DISPLAY) {
                    // Found a Display controller
                    for (int bar = 0; bar < PCI_BAR_COUNT; bar++) {
                        bar_value = pci_read_bar(bus, slot, func, bar);
                        if (bar_value == 0xFFFFFFFF || bar_value == 0)
                            continue; // skip invalid

                        if (bar_value & 1) 
                            continue; // I/O space, not memory

                        uint32_t fb_phys = bar_value & ~0xFU; // mask flags
                        if (fb_phys) {
                            // check if this is a framebuffer-like address
                            if (fb_phys >= 0xE0000000) {
                                return fb_phys; // plausible framebuffer
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback for Bochs/QEMU stdvga
    return 0xE0000000;
}
