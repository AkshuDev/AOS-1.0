#pragma once

#include <inc/core/pcie.h>
#include <inttypes.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

#define MASS_STORAGE_CLASS 0x01
#define SATA_SUBCLASS 0x06
#define AHCI_PROGIF 0x01

#define SATA_PORT_SIGNATURE 0x00000101
#define ATAPI_PORT_SIGNATURE 0xEB140101
#define ENCLOSURE_PORT_SIGNATURE 0xC33C0101
#define PORTMULTIPLIER_PORT_SIGNATURE 0x96690101

#define CMD_ATA_FLUSH_CACHE 0xE7

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
    uint8_t cfl:5;
    uint8_t a:1;
    uint8_t w:1;
    uint8_t p:1;
    uint8_t r:1;
    uint8_t b:1;
    uint8_t c:1;
    uint8_t rsv0:1;
    uint8_t pmp:4;

    uint16_t prdtl;
    uint16_t prdbc;
    uint16_t rsv1;

    uint32_t ctba;
    uint32_t ctbau;
    
    uint32_t rsv2[4];
} __attribute__((packed));

struct sata_hba_prdt_entry {
    uint32_t dba; // data base addr (low)
    uint32_t dbau; // data base addr (high)
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t ioc:1;
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

struct sata_identify {
    uint16_t config; /* 0: General configuration */
    uint16_t cyls; /* 1: Number of cylinders */
    uint16_t res1; /* 2: Reserved */
    uint16_t heads; /* 3: Number of heads */
    uint16_t res2[2]; /* 4-5: Retired */
    uint16_t sectors; /* 6: Number of sectors per track */
    uint16_t vendor[3]; /* 7-9: Vendor specific */
    uint16_t serial[10]; /* 10-19: Serial number (ASCII) */
    uint16_t buffer_type; /* 20: Buffer type */
    uint16_t buffer_size; /* 21: Buffer size / 512 */
    uint16_t ecc_bytes; /* 22: ECC bytes */
    uint16_t fw_rev[4]; /* 23-26: Firmware revision (ASCII) */
    uint16_t model[20]; /* 27-46: Model number (ASCII) */
    uint16_t multi_sect_max; /* 47: Max multiple sect count */
    uint16_t dpw_cap; /* 48: Double word P-I-O */
    uint16_t capabilities[2]; /* 49-50: Capabilities */
    uint16_t pio_timing; /* 51: PIO timing */
    uint16_t dma_timing; /* 52: DMA timing */
    uint16_t field_valid; /* 53: Valid field extension */
    uint16_t cur_cyls; /* 54: Current cylinders */
    uint16_t cur_heads; /* 55: Current heads */
    uint16_t cur_sectors; /* 56: Current sectors */
    uint16_t lba_capacity[2]; /* 57-58: LBA capacity */
    uint16_t multi_sect_set; /* 59: Multi-sector setting */
    uint16_t lba_cap_48[2]; /* 60-61: LBA capacity 48 */
    uint16_t dma_1word; /* 62: Single word DMA */
    uint16_t dma_2word; /* 63: Multiword DMA */
    uint16_t adv_pio; /* 64: Advanced PIO */
    uint16_t mw_dma_cycle; /* 65: MW DMA cycle time */
    uint16_t min_pio_no_flow; /* 66: Min PIO no flow */
    uint16_t min_pio_flow; /* 67: Min PIO w/ flow */
    uint16_t res3[2]; /* 68-69: Reserved */
    uint16_t ata_major; /* 70: Major version */
    uint16_t ata_minor; /* 71: Minor version */
    uint16_t cmd_set_1; /* 72: Command set 1 */
    uint16_t cmd_set_2; /* 73: Command set 2 */
    uint16_t cmd_ext; /* 74: Command set extension */
    uint16_t q_depth; /* 75: Queue depth */
    uint16_t res4[4]; /* 76-79: Reserved */
    uint16_t major_rev; /* 80: Major revision */
    uint16_t minor_rev; /* 81: Minor revision */
    uint16_t features; /* 82: Features */
    uint16_t features_ext; /* 83: Features extension */
    uint16_t features_48; /* 84: Features 48 */
    uint16_t set_feat_1; /* 85: Set features 1 */
    uint16_t set_feat_2; /* 86: Set features 2 */
    uint16_t set_feat_48; /* 87: Set features 48 */
    uint16_t udma_modes; /* 88: UDMA modes */
    uint16_t time_security; /* 89: Security time */
    uint16_t time_download; /* 90: Download time */
    uint16_t power_mgmt; /* 91: Power management */
    uint16_t res5[3]; /* 92-94: Reserved */
    uint16_t stream_min_req; /* 95: Stream min request */
    uint16_t stream_max_req; /* 96: Stream max request */
    uint16_t stream_latency[2]; /* 97-98: Stream latency */
    uint16_t lba_capacity_64[2]; /* 99-100: LBA capacity 64-bit */
    uint16_t res6[26]; /* 101-126: Reserved */
    uint16_t vendor_status; /* 127: Vendor status */
    uint16_t security_status; /* 128: Security status */
    uint16_t res7[31]; /* 129-159: Reserved */
    uint16_t res8[4]; /* 160-163: Reserved */
    uint16_t reserved_164_255[92]; /* 164-255: Reserved */
} __attribute__((packed));

struct sata_port_state {
    uint64_t clb_phys;
    uint64_t clb_virt;
    uint64_t fis_phys;
    uint64_t fis_virt;
    struct sata_hba_cmd_hdr* cmd_hdrs;

    uint8_t active;
    struct sata_hba_port* port;
};

int sata_init(void) __attribute__((used));
void sata_get_pcie(pcie_device_t* out) __attribute__((used));
int sata_read_blk(int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
int sata_write_blk(int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
int sata_flush(int port_id) __attribute__((used));
int sata_get_block_device(int port_id, struct block_device* out) __attribute__((used));
void sata_get_available_ports(uint8_t* out, int out_size) __attribute__((used));