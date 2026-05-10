#include <inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/core/pcie.h>

#include <inc/drivers/io/sata.h>

#include <inc/drivers/io/drive.h>
#include <inc/drivers/io/io.h>

#include <inc/core/module.h>

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

int get_available_drives(struct drive_device* out) {
    serial_print("[Drive Controller] Trying to find registered drives...\n");
    
    struct AOS_Module* reg_driver = module_get_first_applicable_registered_driver(PCI_CLASS_MASS_STORAGE, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!reg_driver) {
        serial_print("[Drive Controller] No registered drives!\n");
        return 0;
    } else if (reg_driver->hdr.type != MODULE_TYPE_DRIVER) {
        serial_print("[Drive Controller] No registered drivers for drives!\n");
        return 0;
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