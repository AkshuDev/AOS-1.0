#include <system.h>
#include <inttypes.h>
#include <asm.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/pcie.h>
#include <inc/core/kfuncs.h>

#include <inc/drivers/io/sata.h>
#include <inc/drivers/io/io.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

static pcie_device_t sata_device = {0};
static uint8_t found_sata = 0;

static volatile struct sata_hba_mem* hba_mem = NULL;

static struct sata_port_state port_states[32] = {0};
static uint8_t ports_available[32] = {0};

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
    port->cmd |= (1 << 2); // POD
    port->cmd |= (1 << 1); // SUD

    asm volatile("mfence" ::: "memory");
    port->cmd |= (1 << 4); // FRE
    asm volatile("mfence" ::: "memory");
    port->cmd |= (1 << 0); // ST
    asm volatile("mfence" ::: "memory");
}

static int sata_find_cmdslot(struct sata_hba_port* port) {
    uint32_t slots = port->sact | port->ci;

    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i)))
            return i;
    }

    return -1;
}

static int sata_port_init(struct sata_hba_port* port, struct sata_port_state* state) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;

    if (det != 3 || ipm != 1)
        return 0;

    sata_port_stop(port);
    if (sata_busy_wait(port) != 1) return 0;

    port->is = 0xFFFFFFFF;
    port->serr = 0xFFFFFFFF;

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

    int slots = ((hba_mem->cap >> 8) & 0x1F) + 1;
    serial_printf("[AHCI] Command slots: %d\n", slots);
    for (int i = 0; i < slots; i++) {
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

    state->port = port;

    sata_port_start(port);
    sata_busy_wait(port);

    port->is = 0xFFFFFFFF;
    port->serr = 0xFFFFFFFF;

    state->active = 1;
    return 1;
}

static void sata_map_bar(void) {
    uint64_t bar5 = pcie_read_bar(sata_device.bus, sata_device.slot, sata_device.func, 5);
    if (bar5 & 1) {
        serial_print("[AHCI] BAR5 is not MMIO!\n");
        return;
    }
    uint64_t phys = bar5 & ~0xF;
    pager_map_range(AOS_AHCI_VIRT_BASE, phys, 0x2000, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    hba_mem = (struct sata_hba_mem*)AOS_AHCI_VIRT_BASE;
}

static int sata_send_cmd(struct sata_port_state* state, uint8_t write, uint64_t lba, uint32_t count, void* buffer, uint8_t command, uint8_t fis_type) {
    serial_print("[AHCI] Sending command...\n");
    struct sata_hba_port* port = state->port;
    if (!sata_busy_wait(port))
        return 0;

    int slot = sata_find_cmdslot(port);
    if (slot < 0) {
        serial_print("[AHCI] No free command slot\n");
        return 0;
    }

    struct sata_hba_cmd_hdr* cmd = &state->cmd_hdrs[slot];
    uint32_t ctba = cmd->ctba;
    uint32_t ctbau = cmd->ctbau;
    memset(cmd, 0, sizeof(struct sata_hba_cmd_hdr));
    cmd->ctba = ctba;
    cmd->ctbau = ctbau;

    cmd->cfl = sizeof(struct sata_fis_reg_h2d) / sizeof(uint32_t);
    cmd->w = write ? 1 : 0;
    cmd->prdtl = 1;

    struct sata_hba_cmd_table* table = (struct sata_hba_cmd_table*)((uint64_t)cmd->ctba | ((uint64_t)cmd->ctbau << 32));
    memset(table, 0, sizeof(struct sata_hba_cmd_table));

    uint64_t phys;
    void* virt = (void*)avmf_alloc(512, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &phys);
    if (!virt || !phys) return 0;

    if (write) memcpy(virt, buffer, 512);

    table->prdt_entry[0].dba = (uint32_t)((uint64_t)phys & 0xFFFFFFFF);
    table->prdt_entry[0].dbau = (uint32_t)((uint64_t)phys >> 32);
    table->prdt_entry[0].dbc = (count * 512) - 1;
    table->prdt_entry[0].ioc = 1;

    struct sata_fis_reg_h2d* fis = (struct sata_fis_reg_h2d*)&table->cfis;

    fis->fis_type = fis_type;
    fis->c = 1;
    fis->command = command;

    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->device = 1 << 6; // LBA mode

    fis->countl = count & 0xFF;
    fis->counth = (count >> 8) & 0xFF;

    port->ci = 1 << slot;
    int timeout = 1000000;
    while (1) {
        if (timeout == 0) break;
        if (!(port->ci & (1 << slot)))
            break;

        if (port->is & (1 << 30)) {
            serial_print("[AHCI] Disk error\n");
            avmf_free((uint64_t)virt);
            return 0;
        }

        timeout--;
    }

    if (!write) memcpy(buffer, virt, 512);
    avmf_free((uint64_t)virt);

    if (timeout == 0) {
        serial_print("[AHCI] Disk/Device Error (timeout)\n");
        return 0;
    }

    serial_print("[AHCI] Command sent successfully!\n");
    return 1;
}

static int sata_issue_cmd(struct sata_port_state* state, int write, uint64_t lba, uint32_t count, void* buffer) {
    serial_print("[AHCI] Sending command...\n");
    struct sata_hba_port* port = state->port;
    if (!sata_busy_wait(port))
        return 0;

    int slot = sata_find_cmdslot(port);
    if (slot < 0) {
        serial_print("[AHCI] No free command slot\n");
        return 0;
    }

    struct sata_hba_cmd_hdr* cmd = &state->cmd_hdrs[slot];
    uint32_t ctba = cmd->ctba;
    uint32_t ctbau = cmd->ctbau;
    memset(cmd, 0, sizeof(struct sata_hba_cmd_hdr));
    cmd->ctba = ctba;
    cmd->ctbau = ctbau;

    cmd->cfl = sizeof(struct sata_fis_reg_h2d) / sizeof(uint32_t);
    cmd->w = write ? 1 : 0;
    cmd->prdtl = 1;

    struct sata_hba_cmd_table* table = (struct sata_hba_cmd_table*)((uint64_t)cmd->ctba | ((uint64_t)cmd->ctbau << 32));
    memset(table, 0, sizeof(struct sata_hba_cmd_table));

    uint64_t phys;
    void* virt = (void*)avmf_alloc(512, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &phys);
    if (!virt || !phys) return 0;

    if (write) memcpy(virt, buffer, 512);

    table->prdt_entry[0].dba = (uint32_t)((uint64_t)phys & 0xFFFFFFFF);
    table->prdt_entry[0].dbau = (uint32_t)((uint64_t)phys >> 32);
    table->prdt_entry[0].dbc = (count * 512) - 1;
    table->prdt_entry[0].ioc = 1;

    struct sata_fis_reg_h2d* fis = (struct sata_fis_reg_h2d*)&table->cfis;

    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = write ? CMD_ATA_WRITE_DMA_EXT : CMD_ATA_READ_DMA_EXT;

    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->device = 1 << 6; // LBA mode

    fis->countl = count & 0xFF;
    fis->counth = (count >> 8) & 0xFF;

    port->ci = 1 << slot;
    int timeout = 1000000;
    while (1) {
        if (timeout == 0) break;
        if (!(port->ci & (1 << slot)))
            break;

        if (port->is & (1 << 30)) {
            serial_print("[AHCI] Disk error\n");
            avmf_free((uint64_t)virt);
            return 0;
        }

        timeout--;
    }

    if (!write) memcpy(buffer, virt, 512);
    avmf_free((uint64_t)virt);

    if (timeout == 0) {
        serial_print("[AHCI] Disk/Device Error (timeout)\n");
        return 0;
    }

    serial_print("[AHCI] Command sent successfully!\n");
    return 1;
}

int sata_init(void) {
    if (pcie_find_ex(
        &sata_device.bus, &sata_device.slot, &sata_device.func,
        (uint32_t*)&sata_device.bar0,
        MASS_STORAGE_CLASS, SATA_SUBCLASS, AHCI_PROGIF, 0, 0
    ) != 1) {
        return 0;
    }

    sata_map_bar();
    if (hba_mem == NULL) return 0;
    pcie_enable_busmaster(sata_device.bus, sata_device.slot, sata_device.func);

    // Reset
    hba_mem->ghc |= (1 << 31); // AHCI

    hba_mem->ghc |= (1 << 0);
    while (hba_mem->ghc & (1 << 0));

    hba_mem->ghc |= (1 << 31); // AHCI
    hba_mem->ghc |= (1 << 1); // Interrupt Enable (IE)

    hba_mem->is = 0xFFFFFFFF;
    uint32_t pi = hba_mem->pi;

    serial_printf("[AHCI] CAP: %08x\n", hba_mem->cap);
    serial_printf("[AHCI] PI: %08x\n", hba_mem->pi);
    serial_printf("[AHCI] VS: %08x\n", hba_mem->vs);

    if (!(hba_mem->cap & (1 << 31))) {
        serial_print("[AHCI] Controller does not support 64-bit DMA\n");
        return 0;
    }

    int ports = (hba_mem->cap & 0x1F) + 1;
    int ports_found = 0;

    for (int i = 0; i < ports; i++) {
        if (pi & (1 << i)) {
            struct sata_hba_port* port = &hba_mem->ports[i];

            uint32_t ssts = port->ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            if (det != 3) continue;

            // Force Stop the port
            port->cmd &= ~0x0001; // Clear ST (bit 0)
            port->cmd &= ~0x0010; // Clear FRE (bit 4)

            // Wait for the HBA to confirm the port is idle
            while(port->cmd & (1 << 15) || port->cmd & (1 << 14));

            // Trigger a COMRESET
            port->sctl = (port->sctl & ~0x0F) | 1; 
            kdelay(1);
            port->sctl &= ~0x0F; // Clear the reset bit to allow comms

            // Re-enable FIS receiving
            port->cmd |= 0x0010; // Set FRE (bit 4)

            int timeout = 1000; 
            while (timeout-- > 0) {
                // Give the FIS a tiny bit of time to arrive and update the signature
                kdelay(10); 
                if (port->sig != 0xFFFFFFFF) break;
                kdelay(1);
            }

            serial_printf("[AHCI] Port with signature [%x] found\n", port->sig);

            switch (port->sig) {
                case SATA_PORT_SIGNATURE:
                    serial_printf("[AHCI] SATA drive on port %d\n", i);
                    break;

                case ATAPI_PORT_SIGNATURE:
                    serial_printf("[AHCI] ATAPI drive on port %d\n", i);
                    break;

                case PORTMULTIPLIER_PORT_SIGNATURE:
                    serial_printf("[AHCI] Port Multiplier on port %d\n", i);
                    continue;

                default:
                    serial_printf("[AHCI] Unknown device on port %d\n", i);
                    continue;
            }
            
            if (det == 3 && ipm == 1) {
                sata_port_init(port, &port_states[i]);
                ports_found++;
                ports_available[i] = 1;
            }
        }
    }

    if (ports_found > 0) {
        found_sata = 1;
        serial_print("[AHCI] Controller Found and online\n");
        return 1;
    } else {
        found_sata = 0;
        serial_print("[AHCI] No Controller was found!\n");
        return 0;
    }
}

int sata_read_blk(int port_id, uint64_t lba, uint32_t count, void* buffer) {
    if (!found_sata)
        return 0;

    struct sata_port_state* state = &port_states[port_id];

    if (!state->active)
        return 0;

    return sata_issue_cmd(state, 0, lba, count, buffer);
}

int sata_write_blk(int port_id, uint64_t lba, uint32_t count, void* buffer) {
    if (!found_sata)
        return 0;

    struct sata_port_state* state = &port_states[port_id];

    if (!state->active)
        return 0;

    return sata_issue_cmd(state, 1, lba, count, buffer);
}

int sata_flush(int port_id) {
    if (!found_sata) return 0;

    struct sata_port_state* state = &port_states[port_id];
    if (!state->active) return 0;

    struct sata_hba_port* port = state->port;
    if (!sata_busy_wait(port)) return 0;

    int slot = sata_find_cmdslot(port);
    if (slot < 0) {
        serial_print("[AHCI] No free command slot for flush\n");
        return 0;
    }

    struct sata_hba_cmd_hdr* cmd = &state->cmd_hdrs[slot];
    memset(cmd, 0, sizeof(struct sata_hba_cmd_hdr));

    cmd->cfl = sizeof(struct sata_fis_reg_h2d) / sizeof(uint32_t);
    cmd->w = 1; // write
    cmd->prdtl = 0; // no data

    struct sata_hba_cmd_table* table = (struct sata_hba_cmd_table*)((uint64_t)cmd->ctba | ((uint64_t)cmd->ctbau << 32));
    memset(table, 0, sizeof(struct sata_hba_cmd_table));

    struct sata_fis_reg_h2d* fis = (struct sata_fis_reg_h2d*)&table->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = CMD_ATA_FLUSH_CACHE;

    port->ci = 1 << slot;
    while (port->ci & (1 << slot)) {
        if (port->is & (1 << 30)) {
            serial_print("[AHCI] Flush failed, disk error\n");
            return 0;
        }
    }

    return 1;
}

static int sata_get_info(int port_id, struct sata_identify* id) {
    if (!found_sata) {
        serial_print("[AHCI] State is not found!\n");
        return 0;
    }

    struct sata_port_state* state = &port_states[port_id];
    if (!state->active) {
        serial_print("[AHCI] State is not active!\n");
        return 0;
    }
    uint8_t buffer[512];

    if (!sata_send_cmd(state, 0, 0, 1, (void*)buffer, CMD_ATA_IDENTIFY, FIS_TYPE_REG_H2D)) {
        serial_print("[AHCI] Failed to issue command!\n");
        return 0;
    }

    memcpy(id, buffer, sizeof(struct sata_identify));
    return 1;
}

int sata_get_block_device(int port_id, struct block_device* out) {
    serial_print("[AHCI] Getting drive info...\n");
    struct sata_identify* idenvirt = (struct sata_identify*)avmf_alloc(sizeof(struct sata_identify), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
    if (sata_get_info(port_id, idenvirt) != 1) {
        serial_print("[AHCI] Failed to get drive info!\n");
        return 0;
    }
    serial_print("[AHCI] Got drive info!\n");
    struct sata_identify iden = *idenvirt;
    uint32_t block_count = ((uint32_t)iden.lba_cap_48[1] << 16) | iden.lba_cap_48[0];
    out->block_count = block_count;
    out->block_size = 512;
    char* model = (char*)avmf_alloc(41, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, NULL);
    if (model == NULL) {
        out->name = NULL;
        return 1;
    }
    for (int i = 0; i < 20; i++) {
        model[i*2] = iden.model[i] >> 8; // high byte
        model[i*2 + 1] = iden.model[i] & 0xFF; // low byte
    }
    model[40] = '\0';
    out->name = (const char*)model;

    return 1;
}

void sata_get_pcie(pcie_device_t *out) {
    memcpy(out, &sata_device, sizeof(pcie_device_t));
}

void sata_get_available_ports(uint8_t* out, int out_size) {
    for (int i = 0; i < 32; i++) {
        if (i + 1 > out_size) break;
        out[i] = ports_available[i];
    }
}