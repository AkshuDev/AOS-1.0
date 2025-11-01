#include <inttypes.h>
#include <asm.h>

#include <inc/pcie.h>
#include <inc/sata.h>
#include <inc/io.h>

#define ENABLE_AHCI_CONTROLLER(controller) (controller->ghc |= (1 << 31))
#define DISABLE_AHCI_CONTROLLER(controller) (controller->ghc &= ~(1 << 31))

void sata_initialize(ahci_controller_t* controller) {
    ENABLE_AHCI_CONTROLLER(controller);

    while ((controller->ghc & (1 << 31)) == 0) {
        // Wait for controller to be enabled
    }

    sata_scan_ports(controller);
}

void sata_scan_ports(ahci_controller_t* controller) {
    for (uint32_t port = 0; port < SATA_PORTS_MAX; port++) {
        if (controller->pi & (1 << port)) { // Check if port is implemented
            ahci_port_t* port_ctrl = &controller->ports[port];
            port_ctrl->cmd_list_base = 0x100000;
            port_ctrl->cmd_list_size = 1024;
            port_ctrl->fis_base = 0x200000;
            port_ctrl->fis_size = 512;
        }
    }
}

void sata_read(uint32_t port, uint64_t lba, void* buffer, uint64_t size) {
    // Need to read more to finish this
}

void sata_write(uint32_t port, uint64_t lba, const void* buffer, uint64_t size) {
    // Need to read more
}