#include <inttypes.h>
#include <inc/core/pcie.h>

#include <inc/drivers/io/sata.h>

#include <inc/drivers/io/drive.h>
#include <inc/drivers/io/io.h>

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
    out->init = sata_init;
    out->read_blk = sata_read_blk;
    out->write_blk = sata_write_blk;
    out->flush = sata_flush;
    out->get_block_device = sata_get_block_device;
    sata_get_block_device(port_id, &out->block_dev);
    out->name = out->block_dev.name;

    out->active = 1;
}

int get_available_drives(struct drive_device* out) {
    serial_print("[Drive Controller] Trying to find drives...\n");
    if (sata_init() == 1) {
        serial_print("[Drive Controller] Found SATA!\n");
        set_sata(out);
        return 1;
    }
    return 0;
}