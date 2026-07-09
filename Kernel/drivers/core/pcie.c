#include <asm.h>
#include <aos_inttypes.h>
#include <system.h>

#include <inc/core/pcie.h>
#include <inc/core/acpi.h>
#include <inc/drivers/io/io.h>

#include <inc/core/module.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

static struct acpi_mcfg* mcfg_table = NULL;
static int mcfg_num_segs = 0;

aos_bool pcie_init() {
    mcfg_table = acpi_get_mcfg();
    if (mcfg_table == NULL) {
        serial_print("[PCIe] Did not get MCFG Table! Using PCI\n");
    } else {
		mcfg_num_segs = (mcfg_table->header.length - sizeof(struct acpi_mcfg)) / sizeof(struct acpi_mcfg_entry);
		serial_printf("[PCIe] MCFG Segment Count : %u\n", mcfg_num_segs);
		for (int i = 0; i < mcfg_num_segs; i++) {
			struct acpi_mcfg_entry* e = &mcfg_table->entries[i];

			uint32_t bus_count = e->end_bus - e->start_bus + 1;
			uint64_t size = (uint64_t)bus_count << 20;
		}
	}
   
    serial_print("[PCIe] Registering Modules...\n");
	for (uint64_t seg = 0; seg < (mcfg_table ? mcfg_num_segs : 1); seg++) {
		uint8_t start_bus = 0;
		uint8_t end_bus = PCI_MAX_BUS-1;
		if (mcfg_table) {
			start_bus = mcfg_table->entries[seg].start_bus;
			end_bus = mcfg_table->entries[seg].end_bus;
		}
		serial_printf("[PCIe] Enumerating Segment %llu (Start Bus - %u , End Bus - %u)\n", seg, start_bus, end_bus);

		for (uint16_t b = start_bus; b <= end_bus; b++) {
			for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
				uint8_t hdr = (pcie_read(b, s, 0, 0x0C) >> 16) & 0xFF;
				uint8_t max_funcs = (hdr & 0x80) ? PCI_MAX_FUNC : 1;
				for (uint8_t f = 0; f < max_funcs; f++) {
					uint32_t data = pcie_read(b, s, f, 0);
					uint16_t vendor = data & 0xFFFF;
					if (vendor == 0xFFFF)
						continue;
					uint32_t class_data = pcie_read(b, s, f, 0x08);
					uint8_t class = (class_data >> 24) & 0xFF;
					uint8_t class_sub = (class_data >> 16) & 0xFF;
					uint8_t class_progif = (class_data >> 8) & 0xFF;
					uint8_t revision = (class_data & 0xFF);

					serial_printf(
						"[PCIe] %02x:%02x.%x Vendor=%04x Class=%02x Sub=%02x IF=%02x DATA=%08x\n",
						b, s, f,
						vendor,
						class,
						class_sub,
						class_progif,
						data
					);

					struct AOS_Module* m = module_get_first_applicable_driver_alloced(class, class_sub, 1, class_progif, 1, revision, 1, vendor, 1);
					if (!m || m->hdr.type != MODULE_TYPE_DRIVER) continue;
					if (module_already_initialized(m)) continue;
					serial_printf("[PCIe] Registering Module %s, Got for Class:%d SClass:%d Progif:%d\n", m->hdr.name, class, class_sub, class_progif);

					m->Modules.driver_module.pcie_device.bus = b;
					m->Modules.driver_module.pcie_device.slot = s;
					m->Modules.driver_module.pcie_device.func = f;
					m->Modules.driver_module.pcie_device.bar0 = pcie_read(b, s, f, 0x10);
					m->Modules.driver_module.pcie_device.class_code = class;
					m->Modules.driver_module.pcie_device.subclass = class_sub;
					m->Modules.driver_module.pcie_device.vendor_id = vendor;
					m->Modules.driver_module.pcie_device.prog_if = class_progif;
					m->Modules.driver_module.pcie_device.device_id = (data >> 16) & 0xFFFF;

					switch (m->Modules.driver_module.type) {
						case MODULE_DRIVER_TYPE_SATA: {
							m->Modules.driver_module.DriverConnections.drive_connector.pcie_device = &m->Modules.driver_module.pcie_device;
							break;
						}
						case MODULE_DRIVER_TYPE_GPU: {
							m->Modules.driver_module.DriverConnections.gpu_connector.pcie_device = &m->Modules.driver_module.pcie_device;
							break;
						}
						default: break;
					}

					if (m->initialize_on_register) {
						if (!m->init_module(m)) {
							serial_printf("[PCIe] Module %s failed to initialize!\n", m->hdr.name);
							avmf_free((uint64_t)m);
							continue;
						}
					}
					module_register(m);
				}
			}
		}
	}
	serial_print("[PCIe] Modules Registered\n");

    return AOS_TRUE;
}

static int get_segment(uint8_t bus) {
    if (mcfg_table) {
        for (int i = 0; i < mcfg_num_segs; i++) {
            struct acpi_mcfg_entry* e = &mcfg_table->entries[i];

            if (bus >= e->start_bus && bus <= e->end_bus) return i;
        }
    }
    return -1;
}

uint32_t pcie_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    return pcie_read(bus, slot, func, PCI_BAR0 + (bar_index * 4));
}

uint32_t pcie_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	offset &= ~3;
    if (mcfg_table) {
        int eidx = get_segment(bus);
        if (eidx >= 0 && eidx < mcfg_num_segs) {
            struct acpi_mcfg_entry* e = &mcfg_table->entries[eidx];
            uint64_t virt_addr = (uint64_t)((AOS_DIRECT_MAP_BASE + e->base_addr) + (((uint64_t)bus - e->start_bus) << 20) + ((uint64_t)slot << 15) + ((uint64_t)func << 12) + (offset));
            return *(volatile uint32_t*)(virt_addr);
        }
    }
    uint32_t addr = (1U << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    asm_outl(0xCF8, addr);
    return asm_inl(0xCFC);
}

aos_bool pcie_find(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0, uint8_t target_class, uint16_t target_vendor, aos_bool use_vendor) {
	for (uint64_t seg = 0; seg < (mcfg_table ? mcfg_num_segs : 1); seg++) {
		uint8_t start_bus = 0;
		uint8_t end_bus = PCI_MAX_BUS-1;
		if (mcfg_table) {
			start_bus = mcfg_table->entries[seg].start_bus;
			end_bus = mcfg_table->entries[seg].end_bus;
		}
		for (uint16_t b = start_bus; b <= end_bus; b++) {
			for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
				uint8_t hdr = (pcie_read(b, s, 0, 0x0C) >> 16) & 0xFF;
				uint8_t max_funcs = (hdr & 0x80) ? PCI_MAX_FUNC : 1;
				for (uint8_t f = 0; f < max_funcs; f++) {
					uint32_t data = pcie_read(b, s, f, 0);
					uint16_t vendor = data & 0xFFFF;
					if (vendor == 0xFFFF) continue;
					if (use_vendor && vendor != target_vendor) continue;
					uint32_t class_data = pcie_read(b, s, f, 0x08);
					uint8_t class = (class_data >> 24) & 0xFF;
					if (class == target_class) {
						*bus = b;
						*slot = s;
						*func = f;
						*bar0 = pcie_read(b, s, f, 0x10);
						return AOS_TRUE;
					}
				}
			}
		}
	}
    return AOS_FALSE;
}

aos_bool pcie_find_ex(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0, uint8_t target_class, uint8_t target_subclass, uint8_t target_progifclass, uint16_t target_vendor, aos_bool use_vendor) {
    // PCIe find Extended
	for (uint64_t seg = 0; seg < (mcfg_table ? mcfg_num_segs : 1); seg++) {
		uint8_t start_bus = 0;
		uint8_t end_bus = PCI_MAX_BUS-1;
		if (mcfg_table) {
			start_bus = mcfg_table->entries[seg].start_bus;
			end_bus = mcfg_table->entries[seg].end_bus;
		}
		for (uint16_t b = start_bus; b <= end_bus; b++) {
			for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
				uint8_t hdr = (pcie_read(b, s, 0, 0x0C) >> 16) & 0xFF;
				uint8_t max_funcs = (hdr & 0x80) ? PCI_MAX_FUNC : 1;
				for (uint8_t f = 0; f < max_funcs; f++) {
					uint32_t data = pcie_read(b, s, f, 0);
					uint16_t vendor = data & 0xFFFF;
					if (vendor == 0xFFFF) continue;
					if (use_vendor && vendor != target_vendor) continue;
					uint32_t class_data = pcie_read(b, s, f, 0x08);
					uint8_t class = (class_data >> 24) & 0xFF;
					uint8_t class_sub = (class_data >> 16) & 0xFF;
					uint8_t class_progif = (class_data >> 8) & 0xFF;
					if (
						class == target_class &&
						class_sub == target_subclass &&
						class_progif == target_progifclass
					) {
						*bus = b;
						*slot = s;
						*func = f;
						*bar0 = pcie_read(b, s, f, 0x10);
						return AOS_TRUE;
					}
				}
			}
		}
	}
	return AOS_FALSE;
}

aos_bool pcie_find_rex(uint8_t* bus, uint8_t* slot, uint8_t* func, uint32_t* bar0, uint8_t target_class, uint8_t target_subclass, uint8_t target_progifclass, uint8_t target_revision, uint16_t target_vendor, aos_bool use_vendor) {
    // Pci find Revision-Extended
	for (uint64_t seg = 0; seg < (mcfg_table ? mcfg_num_segs : 1); seg++) {
		uint8_t start_bus = 0;
		uint8_t end_bus = PCI_MAX_BUS-1;
		if (mcfg_table) {
			start_bus = mcfg_table->entries[seg].start_bus;
			end_bus = mcfg_table->entries[seg].end_bus;
		}
		for (uint16_t b = start_bus; b <= end_bus; b++) {
			for (uint8_t s = 0; s < PCI_MAX_SLOT; s++) {
				uint8_t hdr = (pcie_read(b, s, 0, 0x0C) >> 16) & 0xFF;
				uint8_t max_funcs = (hdr & 0x80) ? PCI_MAX_FUNC : 1;
				for (uint8_t f = 0; f < max_funcs; f++) {
					uint32_t data = pcie_read(b, s, f, 0);
					uint16_t vendor = data & 0xFFFF;
					if (vendor == 0xFFFF) continue;
					if (use_vendor && vendor != target_vendor) continue;
					uint32_t class_data = pcie_read(b, s, f, 0x08);
					uint8_t class = (class_data >> 24) & 0xFF;
					uint8_t class_sub = (class_data >> 16) & 0xFF;
					uint8_t class_progif = (class_data >> 8) & 0xFF;
					uint8_t revision = (class_data & 0xFF);
					if (
						class == target_class &&
						class_sub == target_subclass &&
						class_progif == target_progifclass &&
						revision == target_revision
					) {
						*bus = b;
						*slot = s;
						*func = f;
						*bar0 = pcie_read(b, s, f, 0x10);
						return AOS_TRUE;
					}
				}
			}
		}
	}
	return AOS_FALSE;
}

uint16_t pcie_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	offset &= ~3;
    uint32_t value = pcie_read(bus, slot, func, offset & 0xFC);

    if (offset & 2)
        return (uint16_t)(value >> 16);
    else
        return (uint16_t)(value & 0xFFFF);
}

aos_bool pcie_write_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index, uint32_t value) {
    return pcie_write(bus, slot, func, PCI_BAR0 + (bar_index * 4), value);
}

aos_bool pcie_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
	offset &= ~3;
    if (mcfg_table) {
        int eidx = get_segment(bus);
        if (eidx >= 0 && eidx < mcfg_num_segs) {
            struct acpi_mcfg_entry* e = &mcfg_table->entries[eidx];

            uint64_t virt_addr =(uint64_t)((AOS_DIRECT_MAP_BASE + e->base_addr) + ((uint64_t)e->pcie_segment << 28) + (((uint64_t)bus - e->start_bus) << 20) + ((uint64_t)slot << 15) + ((uint64_t)func << 12) + offset);

            *(volatile uint32_t*)virt_addr = value;
			__asm__ volatile("mfence" ::: "memory");
            return AOS_TRUE;
        }
    }

    // Legacy
    uint32_t addr = (1U << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);

    asm_outl(0xCF8, addr);
    asm_outl(0xCFC, value);
    return AOS_TRUE;
}

aos_bool pcie_toggle_busmaster(uint8_t bus, uint8_t slot, uint8_t func, aos_bool toggle) {
    uint32_t cmd_reg = pcie_read(bus, slot, func, 0x04);
    if (toggle) cmd_reg |= (1 << 2); // bus master enable
	else cmd_reg &= ~(1 << 2); // bus master disable
    return pcie_write(bus, slot, func, 0x04, cmd_reg);
}

aos_bool pcie_toggle_memory_space(uint8_t bus, uint8_t slot, uint8_t func, aos_bool toggle) {
    uint32_t cmd_reg = pcie_read(bus, slot, func, 0x04);
    if (toggle) cmd_reg |= (1 << 1); // memory space enable
	else cmd_reg &= ~(1 << 1); // memory space disable
    return pcie_write(bus, slot, func, 0x04, cmd_reg);
}

aos_bool pcie_get_busmaster_toggled(uint8_t bus, uint8_t slot, uint8_t func) {
	uint32_t cmd_reg = pcie_read(bus, slot, func, 0x04);
	return cmd_reg & (1 << 2);
}

aos_bool pcie_get_memory_space_toggled(uint8_t bus, uint8_t slot, uint8_t func) {
	uint32_t cmd_reg = pcie_read(bus, slot, func, 0x04);
	return cmd_reg & (1 << 1);
}
