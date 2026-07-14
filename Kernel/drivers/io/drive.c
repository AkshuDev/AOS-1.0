#include <aos_inttypes.h>
#include <system.h>
#include <uniboot.h>

#include <inc/core/kfuncs.h>
#include <inc/core/pcie.h>

#include <inc/drivers/io/sata.h>

#include <inc/drivers/io/drive.h>
#include <inc/drivers/io/io.h>

#include <inc/core/module.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

// Legacy-ATA
aos_bool legacy_ata_read(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) {
	struct ATA_DP dp = {
		.lba = lba,
		.count = count
	};
	return (ata_read_sectors(&dp, buffer, (uint8_t)port_id) == 0);
}

aos_bool legacy_ata_write(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) {
	struct ATA_DP dp = {
		.lba = lba,
		.count = count
	};
	return (ata_write_sectors(&dp, buffer, (uint8_t)port_id) == 0);
}

aos_bool legacy_ata_get_block_device(uint64_t cidx, int port_id, struct block_device* out) {
	uniboot_boot_info* sinfo = kget_sysinfo();
	if (!sinfo) return AOS_FALSE;

	ata_identity_t iden = {0};
	ata_identify_device(sinfo->boot_drive_raw, &iden);

	out->block_count = iden.block_count;
	out->block_size = iden.block_size;
	size_t len = strlen(iden.model);
	if (len > 0) {
		out->name = (char*)avmf_alloc(len + 1, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
		if (out->name == NULL) return 0;
		memcpy(out->name, (const void*)iden.model, len);
		((char*)out->name)[len] = '\0';	
	} else {
		out->name = "Unamed Drive";
	}
	
	return AOS_TRUE;
}

// Setters
static aos_bool set_sata(uint64_t cidx, struct drive_device* out) {
    uint8_t avail_ports[32] = {0};
    sata_get_available_ports(cidx, (uint8_t*)avail_ports, sizeof(avail_ports));
    int port_id = -1;
    for (int i = 0; i < sizeof(avail_ports); i++) {
        if (avail_ports[i] == 1) {
            port_id = i;
            break;
        }
    }
    if (port_id < 0) return AOS_FALSE;

    out->cur_port = port_id;

    sata_get_pcie(cidx, out->pcie_device);
    if (!sata_get_block_device(cidx, port_id, &out->block_dev)) return AOS_FALSE;
    out->name = out->block_dev.name;
    out->active = 1;
	
	return AOS_TRUE;
}

static aos_bool set_legacy_ata(struct drive_device* out) {
	uniboot_boot_info* sinfo = kget_sysinfo();
	if (!sinfo) return AOS_FALSE;

	uint8_t drive = sinfo->boot_drive_raw;

    ata_identity_t iden = {0};
	if (ata_identify_device(drive, &iden) != 1) return AOS_FALSE;
	if (!legacy_ata_get_block_device(0, (int)drive, &out->block_dev)) return AOS_FALSE;

	out->name = out->block_dev.name;
	out->pcie_device = NULL;
	out->flush = NULL;
	out->get_block_device = legacy_ata_get_block_device;
	out->init = NULL;
	out->read_blk = legacy_ata_read;
	out->write_blk = legacy_ata_write;

	out->cur_port = drive;
    out->active = 1;

	return AOS_TRUE;
}

aos_bool get_available_drives(struct drive_device* out) {
    serial_print("[Drive Controller] Trying to find registered drives...\n");
    
    struct AOS_Module* reg_driver = module_get_first_applicable_registered_driver(PCI_CLASS_MASS_STORAGE, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!reg_driver) {
		if (ata_exists()) {
			if (!set_legacy_ata(&reg_driver->Modules.driver_module.DriverConnections.drive_connector)) {
				serial_print("[Drive Controller] No registered drives!\n");
				return AOS_FALSE;
			}
			memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));
			serial_printf("[Drive Controller] Using 'Legacy-ATA' driver for '%s' disk\n", reg_driver->Modules.driver_module.DriverConnections.drive_connector.name);
			return AOS_TRUE;
		} else {
        	serial_print("[Drive Controller] No registered drives!\n");
        	return AOS_FALSE;
		}
    } else if (reg_driver->hdr.type != MODULE_TYPE_DRIVER) {
		if (ata_exists()) {
			if (set_legacy_ata(&reg_driver->Modules.driver_module.DriverConnections.drive_connector)) {
				serial_print("[Drive Controller] No registered drives!\n");
				return AOS_FALSE;
			}
			memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));
			serial_printf("[Drive Controller] Using 'Legacy-ATA' driver for '%s' disk\n", reg_driver->Modules.driver_module.DriverConnections.drive_connector.name);
			return AOS_TRUE;
		} else {
        	serial_print("[Drive Controller] No registered drivers for drives!\n");
        	return AOS_FALSE;
		}
    }

    switch (reg_driver->Modules.driver_module.type) {
        case MODULE_DRIVER_TYPE_SATA: {
			if (!reg_driver->initialize_on_register) {
				if (sata_init(reg_driver) != 1) {
					serial_print("[Drive Controller] SATA Initialization failed!\n");
					return AOS_FALSE;
				}
			}
            if (!set_sata(reg_driver->Modules.driver_module.DriverConnections.drive_connector.controller_idx, &reg_driver->Modules.driver_module.DriverConnections.drive_connector)) {
				serial_print("[Drive Controller] No registered drives!\n");
        		return AOS_FALSE;
			}
            break;
        }
        default: {
            serial_print("[Drive Controller] No supported drives found!\n");
            return AOS_FALSE;
        }
    }

    memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));

    serial_printf("[Drive Controller] Using '%s' driver\n", reg_driver->hdr.name);
    return AOS_TRUE;
}

aos_bool get_available_drives_pcie(struct drive_device* out, uniboot_pcie pcie) {
    serial_printf("[Drive Controller] Trying to find registered drives that match %02x:%02x.%x ...\n", pcie.bus, pcie.slot, pcie.func);
    
	uint64_t regcount = (uint64_t)module_get_registered_module_count();
	for (uint64_t i = 0; i < regcount; i++) {
		struct AOS_Module* reg_driver = module_get_applicable_registered_driver(i, PCI_CLASS_MASS_STORAGE, 0, 0, 0, 0, 0, 0, 0, 0);
		if (!reg_driver || reg_driver->hdr.type != MODULE_TYPE_DRIVER) {
			continue;
		}

		if (
			reg_driver->Modules.driver_module.pcie_device.bus != pcie.bus ||
			reg_driver->Modules.driver_module.pcie_device.slot != pcie.slot ||
			reg_driver->Modules.driver_module.pcie_device.func != pcie.func
		) continue;

		switch (reg_driver->Modules.driver_module.type) {
			case MODULE_DRIVER_TYPE_SATA: {
				if (!reg_driver->initialize_on_register) {
					if (sata_init(reg_driver) != 1) {
						serial_print("[Drive Controller] SATA Initialization failed!\n");
						continue;
					}
				}
				if (!set_sata(reg_driver->Modules.driver_module.DriverConnections.drive_connector.controller_idx, &reg_driver->Modules.driver_module.DriverConnections.drive_connector)) {
					serial_print("[Drive Controller] No registered drives!\n");
        			return AOS_FALSE;
				}
				break;
			}
			default: {
				continue;
			}
		}

		memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));

		serial_printf("[Drive Controller] Using '%s' driver\n", reg_driver->hdr.name);
		return AOS_TRUE;
	}
	serial_print("[Drive Controller] No supported drives found!\n");
	return AOS_FALSE;
}
