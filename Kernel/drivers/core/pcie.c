#include <asm.h>
#include <inttypes.h>
#include <system.h>

#include <inc/core/pcie.h>
#include <inc/core/acpi.h>
#include <inc/drivers/io/io.h>

#include <inc/mm/pager.h>

static struct acpi_mcfg* mcfg_table = NULL;
static int mcfg_num_segs = 0;

uint8_t pcie_init() {
    mcfg_table = acpi_get_mcfg();
    if (mcfg_table == NULL) {
        serial_print("[PCIE] Did not get MCFG Table!\n");
        return 0;
    }
    mcfg_num_segs = (mcfg_table->header.length - sizeof(struct acpi_mcfg) - 8) / sizeof(struct acpi_mcfg_entry);
    for (int i = 0; i < mcfg_num_segs; i++) {
        struct acpi_mcfg_entry* e = &mcfg_table->entries[i];

        uint32_t bus_count = e->end_bus - e->start_bus + 1;
        uint64_t size = (uint64_t)bus_count << 20;

        uint64_t virt_base = PCIE_VIRT_BASE + ((uint64_t)e->pcie_segment << 28);
        pager_map_range(virt_base, e->base_addr, size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
    }
    return 1;
}

static int get_segment(uint8_t bus) {
    if (mcfg_table) {
        for (int i = 0; i < mcfg_num_segs; i++) {
            struct acpi_mcfg_entry* e = &mcfg_table->entries[i];

            if (bus >= e->start_bus && bus <= e->end_bus) return i;
        }
    }
    return 0;
}

uint32_t pcie_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    return pcie_read(bus, slot, func, PCI_BAR0 + (bar_index * 4));
}

uint32_t pcie_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    if (mcfg_table) {
        int eidx = get_segment(bus);
        if (eidx >= 0 && eidx < mcfg_num_segs) {
            struct acpi_mcfg_entry* e = &mcfg_table->entries[eidx];
            uint64_t virt_addr = (uint64_t)(PCIE_VIRT_BASE + ((uint64_t)e->pcie_segment << 28) + (((uint64_t)bus - e->start_bus) << 20) + ((uint64_t)slot << 15) + ((uint64_t)func << 12) + (offset));
            return *(volatile uint32_t*)(virt_addr);
        }
    }
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
                
                if (class == PCI_CLASS_MASS_STORAGE && subclass == PCI_SUBCLASS_NVMe) { // NVMe
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

