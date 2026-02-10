#include <asm.h>
#include <system.h>

#include <inc/core/acpi.h>
#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>

#define EBDA_SEG_PTR 0x40E
#define MADT_TYPE_LAPIC 0

static struct acpi_mcfg* mcfg_table = NULL;
static struct acpi_madt* madt_table = NULL;
static uint8_t apic_ids[256];
static uint64_t apic_id_count = 0;

struct acpi_mcfg* acpi_get_mcfg() {
    return mcfg_table;
}

struct acpi_madt* acpi_get_madt() {
    return madt_table;
}

uint64_t acpi_get_lapic_base() {
    return (uint64_t)madt_table->lapic_addr;
}

void acpi_get_apic_info(uint8_t* apic_ids_out, uint64_t* apic_id_count_out) {
    for (uint64_t i = 0; i < apic_id_count; i++) {
        apic_ids_out[i] = apic_ids[i];
    }
    *apic_id_count_out = apic_id_count;
}

static uint8_t acpi_checksum(void* table, uint32_t len) {
    uint8_t sum = 0;
    uint8_t* p = (uint8_t*)table;
    for (uint32_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

static struct acpi_rsdp_descriptor* acpi_find_rsdp(void) {
    // try ebda first
    uint16_t ebda_seg = *(uint16_t*)(EBDA_SEG_PTR + AOS_DIRECT_MAP_BASE); // 0x40E holds seg of ebda
    uint32_t ebda = ((uint32_t)ebda_seg) << 4;
    for (uint64_t addr = (uint64_t)ebda + AOS_DIRECT_MAP_BASE; addr < ebda + 1024 + AOS_DIRECT_MAP_BASE; addr += 16) {
        if (memcmp((char*)addr, "RSD PTR ", 8) == 0) {
            struct acpi_rsdp_descriptor* rsdp = (struct acpi_rsdp_descriptor*)addr;
            if (acpi_checksum(rsdp, 20) == 0) {
                if (rsdp->revision >= 2) {
                    if (acpi_checksum(rsdp, sizeof(struct acpi_rsdp_descriptor_v2)) != 0)
                        return NULL;
                }
                return rsdp;
            }
        }
    }

    // check bios area
    for (uint64_t addr = 0xE0000 + AOS_DIRECT_MAP_BASE; addr < 0x100000 + AOS_DIRECT_MAP_BASE; addr += 16) {
        if (memcmp((char*)addr, "RSD PTR ", 8) == 0) {
            struct acpi_rsdp_descriptor* rsdp = (struct acpi_rsdp_descriptor*)addr;
            if (acpi_checksum(rsdp, 20) == 0) {
                if (rsdp->revision >= 2) {
                    if (acpi_checksum(rsdp, sizeof(struct acpi_rsdp_descriptor_v2)) != 0)
                        return NULL;
                }
                return rsdp;
            }
        }
    }

    return NULL;
}

static void acpi_parse_madt(struct acpi_madt* madt) {
    uint8_t* ptr = (uint8_t*)madt + sizeof(struct acpi_madt);
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (ptr < end) {
        uint8_t type = ptr[0];
        uint8_t len = ptr[1];
        if (type == MADT_TYPE_LAPIC) {
            struct acpi_madt_lapic_entry* lapic = (struct acpi_madt_lapic_entry*)ptr;
            if (lapic->flags & 1) {
                serial_printf("[ACPI : SMP] Core Found: APIC ID %d\n", lapic->apic_id);
                apic_ids[apic_id_count++] = lapic->apic_id;
            }
        }
        ptr += len;
    }
    serial_printf("[ACPI : SMP] Total APIC IDs: %d\n", apic_id_count);
}

static void acpi_parse_rsdt(struct acpi_rsdp_descriptor* rsdp) {
    serial_printf("[ACPI] RSDP Revision - %d\n", (uint32_t)rsdp->revision);
    if (rsdp->revision >= 2) { // 64-bit version
        struct acpi_rsdp_descriptor_v2* rsdp_v2 = (struct acpi_rsdp_descriptor_v2*)(rsdp);
        uintptr_t xsdt_addr = (uintptr_t)((uint64_t)rsdp_v2->xsdt_address + AOS_DIRECT_MAP_BASE);
        struct acpi_xsdt* xsdt = (struct acpi_xsdt*)xsdt_addr;
        
        // verify checksum
        if (acpi_checksum(xsdt, xsdt->header.length)) {
            serial_print("[ACPI] Invalid XSDT checksum!\n");
            return;
        }

        int entries = (xsdt->header.length - sizeof(struct acpi_sdt_header)) / 8;
        serial_printf("[ACPI] Found %d XSDT tables\n", entries);

        for (int i = 0; i < entries; i++) {
            struct acpi_sdt_header* sdt_hdr = (struct acpi_sdt_header*)((uintptr_t)((uint64_t)xsdt->entries[i] + AOS_DIRECT_MAP_BASE));
            serial_printf("[ACPI] Table %d has signature %c%c%c%c\n", i, sdt_hdr->signature[0], sdt_hdr->signature[1], sdt_hdr->signature[2], sdt_hdr->signature[3]);
            if (memcmp(sdt_hdr->signature, "MCFG", 4) == 0) {
                mcfg_table = (struct acpi_mcfg*)sdt_hdr;
                serial_printf("[ACPI] Found MCFG at %p\n", mcfg_table);
            } else if (memcmp(sdt_hdr->signature, "APIC", 4) == 0) {
                madt_table = (struct acpi_madt*)sdt_hdr;
                serial_printf("[ACPI] Found MADT at %p\n\tParsing...\n", madt_table);
                acpi_parse_madt(madt_table);
            }
        }
        return;
    }
    uintptr_t rsdt_addr = (uintptr_t)((uint64_t)rsdp->rsdt_address + AOS_DIRECT_MAP_BASE);
    struct acpi_rsdt* rsdt = (struct acpi_rsdt*)rsdt_addr;

    // verify checksum
    if (acpi_checksum(rsdt, rsdt->header.length)) {
        serial_print("[ACPI] Invalid RSDT checksum!\n");
        return;
    }

    int entries = (rsdt->header.length - sizeof(struct acpi_sdt_header)) / 4;
    serial_printf("[ACPI] Found %d RSDT tables\n", entries);

    for (int i = 0; i < entries; i++) {
        struct acpi_sdt_header* sdt_hdr = (struct acpi_sdt_header*)((uintptr_t)((uint64_t)rsdt->entries[i] + AOS_DIRECT_MAP_BASE));
        serial_printf("[ACPI] Table %d has signature %c%c%c%c\n", i, sdt_hdr->signature[0], sdt_hdr->signature[1], sdt_hdr->signature[2], sdt_hdr->signature[3]);
        if (memcmp(sdt_hdr->signature, "MCFG", 4) == 0) {
            mcfg_table = (struct acpi_mcfg*)sdt_hdr;
            serial_printf("[ACPI] Found MCFG at %p\n", mcfg_table);
        } else if (memcmp(sdt_hdr->signature, "APIC", 4) == 0) {
            madt_table = (struct acpi_madt*)sdt_hdr;
            serial_printf("[ACPI] Found MADT at %p\n\tParsing...\n", madt_table);
            acpi_parse_madt(madt_table);
        }
    }
}

void acpi_init(void) {
    struct acpi_rsdp_descriptor *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        serial_print("[ACPI] RSDP Descriptor Not Found!\n");
        return;
    }

    serial_printf("[ACPI] RSDP Descriptor found at %p\n", (uint64_t)rsdp);
    acpi_parse_rsdt(rsdp);
}

void acpi_reboot(void) {
    asm volatile("cli");
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = asm_inb(0x64);
    }
    asm_outb(0x64, 0xFE);
    asm volatile ("hlt");
}

void acpi_shutdown() {
    asm volatile("cli");
}
