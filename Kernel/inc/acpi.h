#pragma once

#include <inttypes.h>

struct acpi_rsdp_descriptor {
    char signature[8]; // 'RSD PTR'
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address; // 32-bit physical address of RSDT
} __attribute__((packed));

struct acpi_rsdp_descriptor_20 {
    struct acpi_rsdp_descriptor rsdp_descriptor;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdt {
    struct acpi_sdt_header header;
    uint32_t entries[];
} __attribute__((packed));

struct acpi_generic_address_structure {
  uint8_t address_space;
  uint8_t bit_width;
  uint8_t bit_offset;
  uint8_t access_size;
  uint64_t address;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_control;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_len;
    uint8_t gpe1_len;
    uint8_t gpe1_base;
    uint8_t c_state_cnt;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_sz;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;

    // Reserved in ACPI 1.0, used in ACPI 2.0+
    uint16_t boot_arch_flags;
    uint8_t reserved2;
    uint32_t flags;

    struct acpi_generic_address_structure reset_reg;
    uint8_t reset_val;
    uint8_t reserved3[3];

    uint64_t x_firmware_cnt;
    uint64_t x_dsdt;

    struct acpi_generic_address_structure x_pm1a_evt_blk;
    struct acpi_generic_address_structure x_pm1b_evt_blk;
    struct acpi_generic_address_structure x_pm1a_cnt_blk;
    struct acpi_generic_address_structure x_pm1b_cnt_blk;
    struct acpi_generic_address_structure x_pm2_cnt_blk;
    struct acpi_generic_address_structure x_pm_tmr_blk;
    struct acpi_generic_address_structure x_gpe0_blk;
    struct acpi_generic_address_structure x_gpe1_blk;
} __attribute__((packed));

struct acpi_mcfg_entry {
    uint64_t base_addr;
    uint16_t pcie_segment;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t reserved;
} __attribute__((packed));

struct acpi_mcfg {
    struct acpi_sdt_header header;
    uint64_t reserved;
    struct acpi_mcfg_entry entries[];
} __attribute__((packed));

void acpi_init() __attribute__((used));
struct acpi_mcfg* acpi_get_mcfg() __attribute__((used));
void acpi_reboot() __attribute__((used, noreturn));
