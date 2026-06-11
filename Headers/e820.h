#pragma once

#define E820_MAX_ENT 128

#define E820_TYPE_RAM 1
#define E820_TYPE_RESERVED 2
#define E820_TYPE_ACPI_RECLAIM 3
#define E820_TYPE_ACPI_NVS 4
#define E820_TYPE_BAD 5

struct bs1_e820_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t ext;
} __attribute__((packed));

struct bs1_e820 {
    uint32_t entry_count;
    struct bs1_e820_entry entries[];
} __attribute__((packed));