#pragma once

#include <inttypes.h>

#define MASS_STORAGE_CLASS 0x01
#define SATA_SUBCLASS 0x06
#define AHCI_PROGIF 0x01

struct sata_hba_port {
    uint32_t clb; // Command List Base (low)
    uint32_t clbu; // Command List Base (high)
    uint32_t fb; // FIS Base (low)
    uint32_t fbu; // FIS Base (high)
    uint32_t is; // Interrupt Status
    uint32_t ie; // Interrupt Enable
    uint32_t cmd; // Command and Status
    uint32_t rsv0;
    uint32_t tfd; // Task File Data
    uint32_t sig; // Signature
    uint32_t ssts; // SATA Status
    uint32_t sctl; // SATA Control
    uint32_t serr; // SATA Error
    uint32_t sact; // SATA Active
    uint32_t ci; // Command Issue
    uint32_t sntf; // SATA Notification
    uint32_t fbs; // FIS-based switching
    uint32_t rsv1[11];
    uint32_t vendor[4];
} __attribute__((packed));

struct sata_hba_mem {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;

    uint8_t reserved[0xA0 - 0x2C];

    uint8_t vendor[0x100 - 0xA0];

    struct sata_hba_port ports[32];
} __attribute__((packed));

struct sata_hba_cmd_hdr {
    uint16_t flags;
    uint16_t prdtl; // PRDT length

    uint32_t prdbc; // PRDT byte count transferred

    uint32_t ctba; // Command table base addr (low)
    uint32_t ctbau; // Command table base addr (high)

    uint32_t rsv1[4];
} __attribute__((packed));

struct sata_hba_prdt_entry {
    uint32_t dba; // data base addr (low)
    uint32_t dbau; // data base addr (high)
    uint32_t rsv0;
    uint32_t dbc; // byte count (0-based) + flags
} __attribute__((packed));

struct sata_hba_cmd_table {
    uint8_t cfis[64]; // Command FIS
    uint8_t acmd[16]; // ATAPI command
    uint8_t rsv[48];

    struct sata_hba_prdt_entry prdt_entry[1];
} __attribute__((packed));

struct sata_fis_reg_h2d {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;

    uint8_t command;
    uint8_t featurel;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;

    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;

    uint8_t rsv1[4];
} __attribute__((packed));

struct sata_hba_fis {
    uint8_t dsfis[0x1C];
    uint8_t pad0[0x04];

    uint8_t psfis[0x14];
    uint8_t pad1[0x0C];

    uint8_t rfis[0x14];
    uint8_t pad2[0x04];

    uint8_t sdbfis[0x08];

    uint8_t ufis[0x40];

    uint8_t rsv[0x60];
} __attribute__((packed));

struct sata_port_state {
    uint64_t clb_phys;
    uint64_t clb_virt;
    uint64_t fis_phys;
    uint64_t fis_virt;
    struct sata_hba_cmd_hdr* cmd_hdrs;
};

void sata_init(void) __attribute__((used));