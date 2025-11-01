#include <asm.h>

#include <inc/acpi.h>
#include <inc/kfuncs.h>
#include <inc/io.h>

#define EBDA_SEG_PTR 0x40E

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
    uint16_t ebda_seg = *(uint16_t*)EBDA_SEG_PTR; // 0x40E holds seg of ebda
    uint32_t ebda = ((uint32_t)ebda_seg) << 4;
    for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
        if (memcmp((char*)addr, "RSD PTR ", 8) == 0) {
            struct acpi_rsdp_descriptor* rsdp = (struct acpi_rsdp_descriptor*)addr;
            if (acpi_checksum(rsdp, 20) == 0) {
                return rsdp;
            }
        }
    }

    // check bios area
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((char*)addr, "RSD PTR ", 8) == 0) {
            struct acpi_rsdp_descriptor* rsdp = (struct acpi_rsdp_descriptor*)addr;
            if (acpi_checksum(rsdp, 20) == 0) {
                return rsdp;
            }
        }
    }

    return NULL;
}

static void acpi_parse_rsdt(struct acpi_rsdp_descriptor* rsdp) {
    struct acpi_rsdt* rsdt = (struct acpi_rsdt*)(uintptr_t)rsdp->rsdt_address;

    // verify checksum
    if (acpi_checksum(rsdt, rsdt->header.length)) {
        serial_print("[ACPI] Invalid RSDT checksum!\n");
        return;
    }

    int entries = (rsdt->header.length - sizeof(struct acpi_sdt_header)) / 4;
    serial_printf("[ACPI] Found %d tables\n", entries);

    for (int i = 0; i < entries; i++) {
        struct acpi_sdt_header* sdt_hdr = (struct acpi_sdt_header*)(uintptr_t)rsdt->entries[i];
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
