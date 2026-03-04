#include "system.h"
#include <inttypes.h>
#include <asm.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/pcie.h>
#include <inc/core/kfuncs.h>

#include <inc/drivers/io/sata.h>
#include <inc/drivers/io/io.h>

static pcie_device_t sata_device = {0};
static uint8_t found_sata = 0;

static volatile struct sata_hba_mem* hba_mem = NULL;

static struct sata_port_state port_states[32] = {0};

static int sata_busy_wait(struct sata_hba_port* port) {
    int timeout = 1000000;
    while ((port->tfd & (0x80 | 0x08)) && timeout--)
        asm volatile("pause");

    if (timeout == 0) {
        serial_print("[AHCI] Port stuck busy\n");
        return 0;
    }
    return 1;
}

static int sata_port_stop(struct sata_hba_port* port) {
    port->cmd &= ~((1 << 0) | (1 << 4)); // clear ST and FRE

    // Wait until CR (15) and FR (14) clear
    int timeout = 1000000;
    while ((port->cmd & (1 << 15)) && timeout--)
        asm volatile("pause");

    timeout = 1000000;
    while ((port->cmd & (1 << 14)) && timeout--)
        asm volatile("pause");

    return 0;
}

static void sata_port_start(struct sata_hba_port* port) {
    asm volatile("mfence" ::: "memory");
    port->cmd |= (1 << 4); // FRE
    asm volatile("mfence" ::: "memory");
    port->cmd |= (1 << 0); // ST
    asm volatile("mfence" ::: "memory");
}

static int sata_port_init(struct sata_hba_port* port, struct sata_port_state* state) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;

    if (det != 3 || ipm != 1)
        return 0;

    sata_port_stop(port);
    if (sata_busy_wait(port) != 1) return 0;

    state->clb_virt = avmf_alloc(1024, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &state->clb_phys);
    if (state->clb_virt == 0) {
        serial_print("[AHCI] Could not allocate 1024 bytes!\n");
        return 0;
    }
    memset((void*)state->clb_virt, 0, 1024);
    state->fis_virt = avmf_alloc(256, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &state->fis_phys);
    if (state->fis_virt == 0) {
        serial_print("[AHCI] Could not allocate 256 bytes!\n");
        avmf_free(state->clb_virt);
        return 0;
    }
    memset((void*)state->fis_virt, 0, 256);

    state->cmd_hdrs = (struct sata_hba_cmd_hdr*)state->clb_virt;
    for (int i = 0; i < 32; i++) {
        uint64_t ct_phys = 0;
        uint64_t ct_virt = avmf_alloc(256, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &ct_phys);
        if (ct_virt == 0) {
            serial_print("[AHCI] Could not allocate 256 bytes for command table!\n");
            avmf_free(state->clb_virt);
            avmf_free(state->fis_virt);
            return 0;
        }
        memset((void*)ct_virt, 0, 256);

        state->cmd_hdrs[i].prdtl = 0;
        state->cmd_hdrs[i].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
        state->cmd_hdrs[i].ctbau = (uint32_t)(ct_phys >> 32);
    }

    port->clb = (uint32_t)(state->clb_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(state->clb_phys >> 32);

    port->fb = (uint32_t)(state->fis_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(state->fis_phys >> 32);

    sata_port_start(port);
    return 1;
}

static void sata_map_bar(void) {
    uint64_t bar5 = pcie_read_bar(sata_device.bus, sata_device.slot, sata_device.func, 5);
    uint64_t phys = bar5 & ~0xF;
    pager_map_range(AOS_AHCI_VIRT_BASE, phys, 0x2000, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    hba_mem = (struct sata_hba_mem*)AOS_AHCI_VIRT_BASE;
}

void sata_init(void) {
    if (pcie_find_ex(
        &sata_device.bus, &sata_device.slot, &sata_device.func,
        (uint32_t*)&sata_device.bar0,
        MASS_STORAGE_CLASS, SATA_SUBCLASS, AHCI_PROGIF, 0, 0
    ) != 1) {
        return;
    }

    pcie_enable_busmaster(sata_device.bus, sata_device.slot, sata_device.func);
    sata_map_bar();

    // Reset
    hba_mem->ghc |= (1 << 0);
    while (hba_mem->ghc & (1 << 0));

    // Re-enable AHCI mode
    hba_mem->ghc |= (1 << 31);
    hba_mem->ghc |= (1 << 1); // Interrupt Enable (IE)

    hba_mem->is = 0xFFFFFFFF;
    uint32_t pi = hba_mem->pi;

    serial_printf("[AHCI] CAP: %08x\n", hba_mem->cap);
    serial_printf("[AHCI] PI: %08x\n", hba_mem->pi);
    serial_printf("[AHCI] VS: %08x\n", hba_mem->vs);

    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            struct sata_hba_port* port = &hba_mem->ports[i];

            uint32_t ssts = port->ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            port->is = 0xFFFFFFFF;
            port->serr = 0xFFFFFFFF;

            if (det == 3 && ipm == 1) {
                sata_port_init(port, &port_states[i]);
            }
        }
    }

    found_sata = 1;
    serial_print("[AHCI] Controller Found and online\n");
}