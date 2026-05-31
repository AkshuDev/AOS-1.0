#include <inttypes.h>
#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/core/pcie.h>

#include <inc/drivers/io/sata.h>

#include <inc/drivers/io/drive.h>
#include <inc/drivers/io/io.h>

#include <inc/core/module.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

// Legacy-ATA
int legacy_ata_read(int port_id, uint64_t lba, uint32_t count, void* buffer) {
	struct ATA_DP dp = {
		.lba = lba,
		.count = count
	};
	return (ata_read_sectors(&dp, buffer, (uint8_t)port_id) == 0);
}

int legacy_ata_write(int port_id, uint64_t lba, uint32_t count, void* buffer) {
	struct ATA_DP dp = {
		.lba = lba,
		.count = count
	};
	return (ata_write_sectors(&dp, buffer, (uint8_t)port_id) == 0);
}

int legacy_ata_get_block_device(int port_id, struct block_device* out) {
	aos_sysinfo_t* sinfo = kget_sysinfo();
	if (!sinfo) return 0;

	ata_identity_t iden = {0};
	ata_identify_device(sinfo->boot_drive, &iden);

	out->block_count = iden.block_count;
	out->block_size = iden.block_size;
	size_t len = strlen(iden.model);
	if (len > 0) {
		out->name = (char*)avmf_alloc(len + 1, MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
		if (out->name == NULL) return 0;
		memcpy(out->name, (const void*)iden.model, len);
		((char*)out->name)[len] = '\0';	
	} else {
		out->name = "Unamed Drive";
	}
	
	return 1;
}

// Setters
static void set_sata(struct drive_device* out) {
    uint8_t avail_ports[32] = {0};
    sata_get_available_ports((uint8_t*)avail_ports, sizeof(avail_ports));
    int port_id = -1;
    for (int i = 0; i < sizeof(avail_ports); i++) {
        if (avail_ports[i] == 1) {
            port_id = i;
            break;
        }
    }
    if (port_id < 0) return;

    out->cur_port = port_id;

    sata_get_pcie(out->pcie_device);
    sata_get_block_device(port_id, &out->block_dev);
    out->name = out->block_dev.name;

    out->active = 1;
}

static void set_legacy_ata(struct drive_device* out) {
	aos_sysinfo_t* sinfo = kget_sysinfo();
	if (!sinfo) {
		out->cur_port = -1;
		return;
	}
	uint8_t drive = sinfo->boot_drive;

    ata_identity_t iden = {0};
	ata_identify_device(drive, &iden);

	if (!legacy_ata_get_block_device((int)drive, &out->block_dev)) {
		out->cur_port = -1;
		return;
	}

	out->name = out->block_dev.name;
	out->pcie_device = NULL;
	out->flush = NULL;
	out->get_block_device = legacy_ata_get_block_device;
	out->init = NULL;
	out->read_blk = legacy_ata_read;
	out->write_blk = legacy_ata_write;

	out->cur_port = drive;
    out->active = 1;
}

int get_available_drives(struct drive_device* out) {
    serial_print("[Drive Controller] Trying to find registered drives...\n");
    
    struct AOS_Module* reg_driver = module_get_first_applicable_registered_driver(PCI_CLASS_MASS_STORAGE, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!reg_driver) {
		if (ata_exists()) {
			set_legacy_ata(&reg_driver->Modules.driver_module.DriverConnections.drive_connector);
			if (reg_driver->Modules.driver_module.DriverConnections.drive_connector.cur_port < 0) {
				serial_print("[Drive Controller] No registered drives!\n");
				return 0;
			}
			memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));
			serial_printf("[Drive Controller] Using 'Legacy-ATA' driver for '%s' disk\n", reg_driver->Modules.driver_module.DriverConnections.drive_connector.name);
			return 1;
		} else {
        	serial_print("[Drive Controller] No registered drives!\n");
        	return 0;
		}
    } else if (reg_driver->hdr.type != MODULE_TYPE_DRIVER) {
		if (ata_exists()) {
			set_legacy_ata(&reg_driver->Modules.driver_module.DriverConnections.drive_connector);
			if (reg_driver->Modules.driver_module.DriverConnections.drive_connector.cur_port < 0) {
				serial_print("[Drive Controller] No registered drives!\n");
				return 0;
			}
			memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));
			serial_printf("[Drive Controller] Using 'Legacy-ATA' driver for '%s' disk\n", reg_driver->Modules.driver_module.DriverConnections.drive_connector.name);
			return 1;
		} else {
        	serial_print("[Drive Controller] No registered drivers for drives!\n");
        	return 0;
		}
    }

    switch (reg_driver->Modules.driver_module.type) {
        case MODULE_DRIVER_TYPE_SATA: {
            if (sata_init(reg_driver) != 1) {
                serial_print("[Drive Controller] SATA Initialization failed!\n");
                return 0;
            }
            set_sata(&reg_driver->Modules.driver_module.DriverConnections.drive_connector);
            break;
        }
        default: {
            serial_print("[Drive Controller] No supported drives found!\n");
            return 0;
        }
    }

    memcpy(out, &reg_driver->Modules.driver_module.DriverConnections.drive_connector, sizeof(drive_device_t));

    serial_printf("[Drive Controller] Using '%s' driver\n", reg_driver->hdr.name);
    return 1;
}