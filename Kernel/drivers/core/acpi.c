#include <asm.h>
#include <system.h>

#include <inc/core/acpi.h>
#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>

#define EBDA_SEG_PTR 0x40E
#define MADT_TYPE_LAPIC 0

static struct acpi_mcfg* mcfg_table = NULL;
static struct acpi_madt* madt_table = NULL;
static struct acpi_fadt* fadt_table = NULL;

static uint8_t apic_ids[256];
static uint64_t apic_id_count = 0;

static uint32_t pm_timer_port = 0;
static uint8_t pm_timer_24bit = 1;

static spinlock_t acpi_lock = 0;

struct acpi_mcfg* acpi_get_mcfg() {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    struct acpi_mcfg* out = mcfg_table;

    spin_unlock_irqrestore(&acpi_lock, rflags);
    return out;
}

struct acpi_madt* acpi_get_madt() {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    struct acpi_madt* out = madt_table;

    spin_unlock_irqrestore(&acpi_lock, rflags);
    return out;
}

uint64_t acpi_get_lapic_base() {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    uint64_t out = (uint64_t)madt_table->lapic_addr;

    spin_unlock_irqrestore(&acpi_lock, rflags);
    return out;
}

void acpi_get_apic_info(uint8_t* apic_ids_out, uint64_t* apic_id_count_out) {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    for (uint64_t i = 0; i < apic_id_count; i++) {
        apic_ids_out[i] = apic_ids[i];
    }
    *apic_id_count_out = apic_id_count;

    spin_unlock_irqrestore(&acpi_lock, rflags);
}

uint32_t acpi_read_timer(void) { // A new entry to prevent DEADLOCKS due to spinlock
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    if (pm_timer_port == 0) {spin_unlock_irqrestore(&acpi_lock, rflags); return 0; }
    uint32_t out = asm_inl(pm_timer_port);

    spin_unlock_irqrestore(&acpi_lock, rflags);
    return out;
}

static uint32_t acpi_timer_read(void) {
    if (pm_timer_port == 0) return 0;
    return asm_inl(pm_timer_port);
}

void acpi_mdelay(uint64_t ms) {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    uint32_t start = acpi_timer_read();
    uint64_t ticks_needed = ms * 3580;
    uint64_t elapsed = 0;
    uint32_t last = start;

    while (elapsed < ticks_needed) {
        uint32_t current = acpi_timer_read();
        if (current < last) {
            // Handle rollover (24-bit or 32-bit)
            if (pm_timer_24bit)
                elapsed += (current + 0x1000000 - last);
            else
                elapsed += (current + 0x100000000ULL - last);
        } else {
            elapsed += (current - last);
        }
        last = current;
        asm volatile("pause");
    }

    spin_unlock_irqrestore(&acpi_lock, rflags);
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

    aos_sysinfo_t* sysinfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    sysinfo->apic_present = 1;
}

static void acpi_timer_init(struct acpi_fadt* fadt) {
    pm_timer_port = fadt->pm_tmr_blk;
    if (fadt->flags & (1 << 8)) {
        pm_timer_24bit = 0;
    }
    serial_printf("[ACPI] PM Timer initialized on port 0x%x\n", pm_timer_port);
}

static void acpi_parse_fadt(struct acpi_fadt* fadt) {
    acpi_timer_init(fadt);
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
            } else if (memcmp(sdt_hdr->signature, "FACP", 4) == 0) {
                fadt_table = (struct acpi_fadt*)sdt_hdr;
                serial_printf("[ACPI] Found FADT at %p\n\tParsing...\n", fadt_table);
                acpi_parse_fadt(fadt_table);
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
        } else if (memcmp(sdt_hdr->signature, "FACP", 4) == 0) {
            fadt_table = (struct acpi_fadt*)sdt_hdr;
            serial_printf("[ACPI] Found FADT at %p\n\tParsing...\n", fadt_table);
            acpi_parse_fadt(fadt_table);
        }
    }
}

void acpi_init(void) {
    uint64_t rflags = spin_lock_irqsave(&acpi_lock);

    struct acpi_rsdp_descriptor *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        serial_print("[ACPI] RSDP Descriptor Not Found!\n");
        return;
    }

    serial_printf("[ACPI] RSDP Descriptor found at %p\n", (uint64_t)rsdp);
    acpi_parse_rsdt(rsdp);

    spin_unlock_irqrestore(&acpi_lock, rflags);
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
    asm volatile("cli ; hlt");
}
