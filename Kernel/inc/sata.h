#pragma once

#include <inttypes.h>
#include <asm.h>

#include <inc/pcie.h>

#define SATA_PORTS_MAX 32
#define SATA_SECTORSIZE 512

// AHCI Register Offsets
#define AHCI_GHC (0x04) // Global Host Control Register
#define AHCI_PI  (0x10) // Port Implemented Register
#define AHCI_PORTS 32 // Number of possible SATA ports

// AHCI Command Register Offsets for a given port
#define AHCI_PORT_CMD (0x00) // Command Register
#define AHCI_PORT_TFD (0x28) // Task File Data Register

typedef struct {
    uint32_t cmd_list_base;
    uint32_t cmd_list_size;
    uint32_t fis_base;
    uint32_t fis_size;
} ahci_port_t;

typedef struct {
    uint32_t ghc; // Global Host Control
    uint32_t pi; // Port Implemented
    uint32_t version;
    uint32_t reserved[5];
    ahci_port_t ports[SATA_PORTS_MAX];
} ahci_controller_t;

// Function prototypes
void sata_initialize(ahci_controller_t* controller) __attribute__((used));
void sata_scan_ports(ahci_controller_t* controller) __attribute__((used));
void sata_read(uint32_t port, uint64_t lba, void* buffer, uint64_t size) __attribute__((used));
void sata_write(uint32_t port, uint64_t lba, const void* buffer, uint64_t size) __attribute__((used));