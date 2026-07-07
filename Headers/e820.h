#pragma once

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

#define E820_MAX_ENT (0xFFFFFFFF / sizeof(struct bs1_e820_entry))

static inline const char* e820_get_type_str(uint32_t type) {
	switch (type) {
		case E820_TYPE_RAM: return "System RAM";
		case E820_TYPE_RESERVED: return "Reserved";
		case E820_TYPE_ACPI_RECLAIM: return "ACPI Reclaim";
		case E820_TYPE_ACPI_NVS: return "ACPI NVS";
		case E820_TYPE_BAD: return "Bad RAM";
		default: return "Unknown";
	}
}