#include <system.h>
#include <aos_inttypes.h>
#include <asm.h>

#include <inc/core/module.h>

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

#define KSATA_MAX_PORTS 32
#define KSATA_ALLOC_STEP 16
#define KSATA_MAX_CONTROLLERS 256

#define AHCI_MAX_PRD_LENGTH (4 * 1024 * 1024)

typedef struct {
	uint64_t idx;
	aos_bool valid;

	pcie_device_t sata_device;
	aos_bool found_sata;

	volatile struct sata_hba_mem* hba_mem;
	uint64_t mapping_size;

	struct sata_port_state port_states[KSATA_MAX_PORTS];
	uint8_t ports_available[32];

	spinlock_t drive_lock;
} sata_controller;

static sata_controller* controllers;
static uint64_t controller_count;
static uint64_t controller_cap;

static aos_bool sata_busy_wait(struct sata_hba_port* port) {
    uint64_t timeout = kget_ms_passed();
	uint64_t ctime = kget_ms_passed();
    while ((port->tfd & (0x80 | 0x08)) && ctime - timeout < 1000) { // 1 second accurate timeout
        asm volatile("pause");
		ctime = kget_ms_passed();
	}

    if (ctime - timeout >= 1000) {
        serial_print("[AHCI] Port stuck busy\n");
        return AOS_FALSE;
    }
    return AOS_TRUE;
}

static aos_bool sata_port_stop(struct sata_hba_port* port) {
    port->cmd &= ~((1 << 0) | (1 << 4)); // clear ST and FRE
	__asm__ volatile("mfence" ::: "memory");

	uint64_t timeout = kget_ms_passed();

    // Wait until CR (15) and FR (14) clear
    while ((port->cmd & (1 << 15)) && kget_ms_passed() - timeout < 1000)
        asm volatile("pause");

    timeout = kget_ms_passed();;
    while ((port->cmd & (1 << 14)) && kget_ms_passed() - timeout < 1000)
        asm volatile("pause");

    return AOS_TRUE;
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

static void sata_destroy(sata_controller* ksc) {
	if (!ksc) return;

	for (uint8_t i = 0; i < KSATA_MAX_PORTS; i++) {
		if (ksc->port_states[i].active) {
			sata_port_stop(ksc->port_states[i].port);
		}
		ksc->ports_available[i] = AOS_FALSE;
	}

	ksc->found_sata = AOS_FALSE;
	ksc->valid = AOS_FALSE;
	if (ksc->idx == controller_count-1) controller_count--;
}

static aos_bool sata_port_init(sata_controller* ksc, struct sata_hba_port* port, struct sata_port_state* state) {
	if (!ksc) return AOS_FALSE;

    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;

    if (det != 3 || ipm != 1)
        return AOS_FALSE;

    sata_port_stop(port);
    if (sata_busy_wait(port) != 1) return AOS_FALSE;

    port->is = 0xFFFFFFFF;
    port->serr = 0xFFFFFFFF;
	__asm__ volatile("mfence" ::: "memory");

    state->clb_virt = avmf_alloc(1024, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &state->clb_phys);
    if (state->clb_virt == 0) {
        serial_print("[AHCI] Could not allocate 1024 bytes!\n");
        return AOS_FALSE;
    }
    memset((void*)state->clb_virt, 0, 1024);
    state->fis_virt = avmf_alloc(256, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &state->fis_phys);
    if (state->fis_virt == 0) {
        serial_print("[AHCI] Could not allocate 256 bytes!\n");
        avmf_free(state->clb_virt);
        return AOS_FALSE;
    }
    memset((void*)state->fis_virt, 0, 256);

    state->cmd_hdrs = (struct sata_hba_cmd_hdr*)state->clb_virt;

    int slots = ((ksc->hba_mem->cap >> 8) & 0x1F) + 1;
    serial_printf("[AHCI] Command slots: %d\n", slots);
    for (int i = 0; i < slots; i++) {
        uint64_t ct_phys = 0;
        uint64_t ct_virt = avmf_alloc(256, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, &ct_phys);
        if (ct_virt == 0) {
            serial_print("[AHCI] Could not allocate 256 bytes for command table!\n");
            avmf_free(state->clb_virt);
            avmf_free(state->fis_virt);
            return AOS_FALSE;
        }
        memset((void*)ct_virt, 0, 256);

        state->cmd_hdrs[i].prdtl = 0;
        state->cmd_hdrs[i].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
        state->cmd_hdrs[i].ctbau = (uint32_t)(ct_phys >> 32);
		__asm__ volatile("mfence" ::: "memory");
    }

    port->clb = (uint32_t)(state->clb_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(state->clb_phys >> 32);

    port->fb = (uint32_t)(state->fis_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(state->fis_phys >> 32);
	__asm__ volatile("mfence" ::: "memory");

    state->port = port;

    sata_port_start(port);
    sata_busy_wait(port);

    port->is = 0xFFFFFFFF;
    port->serr = 0xFFFFFFFF;
	__asm__ volatile("mfence" ::: "memory");

    state->active = AOS_TRUE;
    return AOS_TRUE;
}

static aos_bool sata_map_bar(sata_controller* ksc) {
	if (!ksc) return AOS_FALSE;

    uint8_t bus = ksc->sata_device.bus;
    uint8_t slot = ksc->sata_device.slot;
    uint8_t func = ksc->sata_device.func;

    uint32_t bar5 = pcie_read_bar(bus, slot, func, 5);
    uint64_t bar_phys = bar5 & ~0xFULL;
	if ((bar_phys & 0xFFF) != 0) { // BAR should be page-aligned
		serial_print("[AHCI] BAR5 is not page-aligned, mapping failed!\n");
		return AOS_FALSE;
	}

    aos_bool is_64bit = ((bar5 >> 1) & 0b011) == 0x2;
	uint32_t orig0 = bar5;
    uint32_t orig1 = 0;

	if ((bar5 & 0b001) == 0x1) {
		serial_print("[AHCI] BAR5 is not a memory BAR, mapping failed!\n");
		return AOS_FALSE;
	}

    if (is_64bit) {
        orig1 = pcie_read_bar(bus, slot, func, 6);
        bar_phys |= ((uint64_t)orig1 << 32);
    }

	pcie_toggle_memory_space(bus, slot, func, AOS_FALSE);
	
    pcie_write_bar(bus, slot, func, 5, 0xFFFFFFFF);
    if (is_64bit) pcie_write_bar(bus, slot, func, 6, 0xFFFFFFFF);

    uint32_t mask0 = pcie_read_bar(bus, slot, func, 5);
    uint32_t mask1 = is_64bit ? pcie_read_bar(bus, slot, func, 6) : 0;

    pcie_write_bar(bus, slot, func, 5, orig0);
    if (is_64bit) pcie_write_bar(bus, slot, func, 6, orig1);

	pcie_toggle_memory_space(bus, slot, func, AOS_TRUE);

	uint64_t size = 0;
	if (is_64bit) {
		uint64_t mask = (mask0 & ~0xFULL) | ((uint64_t)mask1 << 32);
		size = ~mask + 1;
	} else {
		uint32_t mask = (uint32_t)(mask0 & ~0xFULL);
		size = (uint32_t)(~mask + 1);
	}

    ksc->mapping_size = size;
	if (ksc->mapping_size < 0x100) {
		serial_print("[AHCI] Device reported memory size is lower than minimum, mapping failed!\n");
		return AOS_FALSE;
	}

	pager_map_range(AOS_DIRECT_MAP_BASE + bar_phys, bar_phys, ksc->mapping_size, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	pcie_toggle_busmaster(bus, slot, func, AOS_TRUE);

	ksc->hba_mem = (struct sata_hba_mem*)(AOS_DIRECT_MAP_BASE + bar_phys);
	serial_printf("[AHCI] Mapped all AHCI (Size: 0x%llx)\n", ksc->mapping_size);
	return AOS_TRUE;
}

static aos_bool sata_exec_cmd_internal(struct sata_port_state* state, uint8_t command, uint8_t fis_type, aos_bool write, uint64_t lba, uint32_t count, void* buffer, aos_bool has_data, aos_bool has_lba, aos_bool has_count) {
	if (!state)
		return AOS_FALSE;

	if (!state->active)
		return AOS_FALSE;

	uint64_t bytes = ((uint64_t)count) * 512;
	uint64_t prdt_count = (bytes + (AHCI_MAX_PRD_LENGTH - 1)) / AHCI_MAX_PRD_LENGTH;

	if (prdt_count > 0xFFFF || prdt_count > SATA_MAX_PRDT_STATIC_ENTRIES) return AOS_FALSE;

	serial_print("[AHCI] Executing command...\n");
    struct sata_hba_port* port = state->port;
    if (!sata_busy_wait(port))
        return AOS_FALSE;

    int slot = sata_find_cmdslot(port);
    if (slot < 0) {
        serial_print("[AHCI] No free command slot\n");
        return AOS_FALSE;
    }

    struct sata_hba_cmd_hdr* cmd = &state->cmd_hdrs[slot];

    uint32_t ctba = cmd->ctba;
    uint32_t ctbau = cmd->ctbau;
    memset(cmd, 0, sizeof(struct sata_hba_cmd_hdr));
    cmd->ctba = ctba;
    cmd->ctbau = ctbau;

    cmd->cfl = sizeof(struct sata_fis_reg_h2d) / sizeof(uint32_t);
    cmd->w = write ? 1 : 0;
    cmd->prdtl = has_data ? (uint16_t)prdt_count : 0;

	uint64_t table_phys = ((uint64_t)cmd->ctbau << 32) | cmd->ctba;
    struct sata_hba_cmd_table* table = (struct sata_hba_cmd_table*)(AOS_DIRECT_MAP_BASE + table_phys);
    
    memset(table, 0, sizeof(struct sata_hba_cmd_table));

    uint64_t phys = 0;
    void* virt = NULL;
	if (has_data) {
		virt = (void*)avmf_alloc(bytes, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &phys);
    	if (!virt) return AOS_FALSE;
		if (!phys) {
			avmf_free((uint64_t)virt);
			return AOS_FALSE;
		}

		if (write) memcpy(virt, buffer, bytes);

		uint64_t bytes_remaining = bytes;
    	uintptr_t current_address = (uintptr_t)phys;
		uint64_t prdt_idx = 0;
		while (bytes_remaining > 0) {
			uint64_t chunk_size = bytes_remaining;
			if (chunk_size > AHCI_MAX_PRD_LENGTH) chunk_size = AHCI_MAX_PRD_LENGTH;

			table->prdt_entry[prdt_idx].dba = (uint32_t)((uint64_t)current_address & 0xFFFFFFFF);
			table->prdt_entry[prdt_idx].dbau = (uint32_t)((uint64_t)current_address >> 32);
			table->prdt_entry[prdt_idx].dbc = (uint32_t)(chunk_size - 1);
			table->prdt_entry[prdt_idx].ioc = prdt_idx == (prdt_count - 1) ? 1 : 0;

			current_address += chunk_size;
			bytes_remaining -= chunk_size;
			prdt_idx++;
		}
	}

    struct sata_fis_reg_h2d* fis = (struct sata_fis_reg_h2d*)&table->cfis;

    fis->fis_type = fis_type;
    fis->c = 1;
    fis->command = command;

	if (has_lba) {
		fis->lba0 = (uint8_t)lba;
		fis->lba1 = (uint8_t)(lba >> 8);
		fis->lba2 = (uint8_t)(lba >> 16);
		fis->lba3 = (uint8_t)(lba >> 24);
		fis->lba4 = (uint8_t)(lba >> 32);
		fis->lba5 = (uint8_t)(lba >> 40);

		fis->device = 1 << 6; // LBA mode
	}

	if (has_count) {
		fis->countl = count & 0xFF;
		fis->counth = (count >> 8) & 0xFF;
	}

	asm volatile("mfence" ::: "memory");
	port->is = 0xFFFFFFFF;
    port->ci |= (1 << slot);
	__asm__ volatile("mfence" ::: "memory");
    uint64_t timeout = kget_ms_passed();
	aos_bool timed_out = AOS_FALSE;
    while (1) {
        if (kget_ms_passed() - timeout > 1000) {timed_out = AOS_TRUE; break;}
        if (!(port->ci & (1 << slot)))
            break;

        if (port->is & (1 << 30)) {
            serial_print("[AHCI] Disk error\n");
            if (virt) avmf_free((uint64_t)virt);
			port->is = 0xFFFFFFFF;
            return AOS_FALSE;
        }
    }

    if (timed_out) {
		if (virt) avmf_free((uint64_t)virt);
        serial_print("[AHCI] Disk/Device Error (timeout)\n");
        return AOS_FALSE;
    }

	if (has_data && !write) memcpy(buffer, virt, bytes);
    if (virt) avmf_free((uint64_t)virt);

    serial_print("[AHCI] Command sent successfully!\n");
    return AOS_TRUE;
}

static aos_bool sata_exec_cmd(struct sata_port_state* state, uint8_t command, uint8_t fis_type, aos_bool write, uint64_t lba, uint32_t count, void* buffer, aos_bool has_data, aos_bool has_lba, aos_bool has_count) {
	if (!state)
		return AOS_FALSE;

	if (!state->active)
		return AOS_FALSE;

	uint64_t bytes = ((uint64_t)count) * 512;
	uint64_t prdt_count = (bytes + (AHCI_MAX_PRD_LENGTH - 1)) / AHCI_MAX_PRD_LENGTH;

	if (prdt_count > 0xFFFF || prdt_count > SATA_MAX_PRDT_STATIC_ENTRIES) {
		uint64_t max_bytes_per_cmd = SATA_MAX_PRDT_STATIC_ENTRIES * AHCI_MAX_PRD_LENGTH;
		uint64_t max_sectors_per_cmd = max_bytes_per_cmd / 512;

		// Do multiple executions
		uint64_t remaining_sectors = count;
		uint64_t current_lba = lba;
		uint8_t* current_buffer = (uint8_t*)buffer;
		while (remaining_sectors) {
			uint64_t sectors_this_cmd = (remaining_sectors > max_sectors_per_cmd) ? max_sectors_per_cmd : remaining_sectors;

			if (
				!sata_exec_cmd_internal(
					state,
					command,
					fis_type,
					write,
					current_lba,
					sectors_this_cmd,
					current_buffer,
					has_data,
					has_lba,
					has_count
				)
			) return AOS_FALSE;

			current_lba += sectors_this_cmd;
			if (has_data) current_buffer += sectors_this_cmd * 512;
			remaining_sectors -= sectors_this_cmd;
		}

		return AOS_TRUE;
	}

	return sata_exec_cmd_internal(
		state,
		command,
		fis_type,
		write,
		lba,
		count,
		buffer,
		has_data,
		has_lba,
		has_count
	);
}

aos_bool sata_init(struct AOS_Module* m) {
    if (m->hdr.type != MODULE_TYPE_DRIVER) return AOS_FALSE;
    if (m->Modules.driver_module.type != MODULE_DRIVER_TYPE_SATA) return AOS_FALSE;

	if (!controllers) {
		controllers = (sata_controller*)avmf_alloc(sizeof(sata_controller) * KSATA_ALLOC_STEP, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!controllers) return AOS_FALSE;
		controller_cap = KSATA_ALLOC_STEP;
		controller_count = 0;
	} else if (controller_count >= controller_cap) {
		if (controller_count >= KSATA_MAX_CONTROLLERS) return AOS_FALSE;
		sata_controller* nptr = (sata_controller*)avmf_alloc(sizeof(sata_controller) * (controller_cap + KSATA_ALLOC_STEP), MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, NULL);
		if (!nptr) return AOS_FALSE;
		memcpy(nptr, controllers, sizeof(sata_controller)*controller_count);
		avmf_free((uint64_t)controllers);
		controllers = nptr;
		controller_cap += KSATA_ALLOC_STEP;
	}

	sata_controller* ksc = &controllers[controller_count];
	memset(ksc, 0, sizeof(sata_controller));
	ksc->idx = controller_count;
	controller_count++;
	m->Modules.driver_module.DriverConnections.drive_connector.controller_idx = ksc->idx;

    ksc->sata_device = m->Modules.driver_module.pcie_device;
	ksc->valid = AOS_FALSE;

    if (!sata_map_bar(ksc)) {
		sata_destroy(ksc);
		return AOS_FALSE;
	}
    // Reset
    ksc->hba_mem->ghc |= (1 << 31); // AHCI

    ksc->hba_mem->ghc |= (1 << 0);
    while (ksc->hba_mem->ghc & (1 << 0));

    ksc->hba_mem->ghc |= (1 << 31); // AHCI
    ksc->hba_mem->ghc |= (1 << 1); // Interrupt Enable (IE)

    ksc->hba_mem->is = 0xFFFFFFFF;
    uint32_t pi = ksc->hba_mem->pi;

    serial_printf("[AHCI] CAP: %08x\n", ksc->hba_mem->cap);
    serial_printf("[AHCI] PI: %08x\n", ksc->hba_mem->pi);
    serial_printf("[AHCI] VS: %08x\n", ksc->hba_mem->vs);

    if (!(ksc->hba_mem->cap & (1 << 31))) {
        serial_print("[AHCI] Controller does not support 64-bit DMA\n");
		sata_destroy(ksc);
        return AOS_FALSE;
    }

    int ports = (ksc->hba_mem->cap & 0x1F) + 1;
    int ports_found = 0;
	if (ports > KSATA_MAX_PORTS) return AOS_FALSE;

    for (int i = 0; i < ports; i++) {
        if (pi & (1 << i)) {
            struct sata_hba_port* port = &ksc->hba_mem->ports[i];

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
            kdelay_us(360);
            port->sctl &= ~0x0F; // Clear the reset bit to allow comms

            // Re-enable FIS receiving
            port->cmd |= 0x0010; // Set FRE (bit 4)

            uint64_t timeout = kget_ms_passed(); 
            while (kget_ms_passed() - timeout < 1000) {
                // Give the FIS a tiny bit of time to arrive and update the signature
                kdelay(10); 
                if (port->sig != 0xFFFFFFFF) break;
                kdelay_us(360);
				asm volatile("pause");
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
                sata_port_init(ksc, port, &ksc->port_states[i]);
                ports_found++;
                ksc->ports_available[i] = 1;
            }
        }
    }

    if (ports_found > 0) {
        ksc->found_sata = AOS_TRUE;
		ksc->valid = AOS_TRUE;
        serial_print("[AHCI] Controller Found and online\n");
        return AOS_TRUE;
    } else {
        sata_destroy(ksc);
        serial_print("[AHCI] No Controller was found!\n");
        return AOS_FALSE;
    }
}

aos_bool sata_read_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) {
	if (cidx >= controller_count || port_id >= KSATA_MAX_PORTS) return AOS_FALSE; 
	sata_controller* ksc = &controllers[cidx];
    if (!ksc->found_sata)
        return AOS_FALSE;

    struct sata_port_state* state = &ksc->port_states[port_id];

    if (!state->active)
        return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&ksc->drive_lock);

    aos_bool out = sata_exec_cmd(
        state,
        CMD_ATA_READ_DMA_EXT,
        FIS_TYPE_REG_H2D,
        AOS_FALSE,
        lba,
        count,
        buffer,
        AOS_TRUE,
        AOS_TRUE,
        AOS_TRUE
    );

	spin_unlock_irqrestore(&ksc->drive_lock, rflags);
	return out;
}

aos_bool sata_write_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) {
	if (cidx >= controller_count || port_id >= KSATA_MAX_PORTS) return AOS_FALSE; 
	sata_controller* ksc = &controllers[cidx];
    if (!ksc->found_sata)
        return AOS_FALSE;

    struct sata_port_state* state = &ksc->port_states[port_id];

    if (!state->active)
        return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&ksc->drive_lock);

    aos_bool out = sata_exec_cmd(
        state,
        CMD_ATA_WRITE_DMA_EXT,
        FIS_TYPE_REG_H2D,
        AOS_TRUE,
        lba,
        count,
        buffer,
        AOS_TRUE,
        AOS_TRUE,
        AOS_TRUE
    );

	spin_unlock_irqrestore(&ksc->drive_lock, rflags);
	return out;
}

aos_bool sata_flush(uint64_t cidx, int port_id) {
	if (cidx >= controller_count || port_id >= KSATA_MAX_PORTS) return AOS_FALSE; 
	sata_controller* ksc = &controllers[cidx];
    if (!ksc->found_sata) return AOS_FALSE;

    struct sata_port_state* state = &ksc->port_states[port_id];
    if (!state->active) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&ksc->drive_lock);

    aos_bool out = sata_exec_cmd(
        state,
        CMD_ATA_FLUSH_CACHE_EXT,
        FIS_TYPE_REG_H2D,
        AOS_FALSE,
        0,
        0,
        NULL,
        AOS_FALSE,
        AOS_FALSE,
        AOS_FALSE
    );

	spin_unlock_irqrestore(&ksc->drive_lock, rflags);
	return out;
}

static aos_bool sata_get_info(uint64_t cidx, int port_id, struct sata_identify* id) {
	if (cidx >= controller_count || port_id >= KSATA_MAX_PORTS) return AOS_FALSE; 
	sata_controller* ksc = &controllers[cidx];
    if (!ksc->found_sata) {
        serial_print("[AHCI] State is not found!\n");
        return AOS_FALSE;
    }

    struct sata_port_state* state = &ksc->port_states[port_id];
    if (!state->active) {
        serial_print("[AHCI] State is not active!\n");
        return AOS_FALSE;
    }
    
	uint64_t rflags = spin_lock_irqsave(&ksc->drive_lock);

	uint8_t buffer[512];

    if (
		!sata_exec_cmd(
			state,
			CMD_ATA_IDENTIFY,
			FIS_TYPE_REG_H2D,
			AOS_FALSE,
			0,
			1,
			buffer,
			AOS_TRUE,
			AOS_FALSE,
			AOS_FALSE
		)
	) {
		spin_unlock_irqrestore(&ksc->drive_lock, rflags);
        serial_print("[AHCI] Failed to issue command!\n");
        return AOS_FALSE;
    }
	spin_unlock_irqrestore(&ksc->drive_lock, rflags);
    memcpy(id, buffer, sizeof(struct sata_identify));
    return AOS_TRUE;
}

aos_bool sata_get_block_device(uint64_t cidx, int port_id, struct block_device* out) {
    serial_print("[AHCI] Getting drive info...\n");
	char buf[sizeof(struct sata_identify)];
    struct sata_identify* idenvirt = (struct sata_identify*)buf;
    if (!sata_get_info(cidx, port_id, idenvirt)) {
        serial_print("[AHCI] Failed to get drive info!\n");
        return AOS_FALSE;
    }
    serial_print("[AHCI] Got drive info!\n");
    struct sata_identify iden = *idenvirt;
	uint64_t block_count = ((uint64_t)iden.lba_cap_48[3] << 48) |
    ((uint64_t)iden.lba_cap_48[2] << 32) |
    ((uint64_t)iden.lba_cap_48[1] << 16) |
    ((uint64_t)iden.lba_cap_48[0]);
    out->block_count = block_count;
    out->block_size = 512;
    char* model = (char*)avmf_alloc(41, MALLOC_TYPE_DRIVER, PAGE_RW | PAGE_PRESENT, NULL);
    if (model == NULL) {
        out->name = NULL;
        return AOS_TRUE;
    }
    for (int i = 0; i < 20; i++) {
        model[i*2] = iden.model[i] >> 8; // high byte
        model[i*2 + 1] = iden.model[i] & 0xFF; // low byte
    }
    model[40] = '\0';
    out->name = (const char*)model;

    return AOS_TRUE;
}

void sata_get_pcie(uint64_t cidx, pcie_device_t *out) {
	if (cidx >= controller_count) return;
	sata_controller* ksc = &controllers[cidx];
    memcpy(out, &ksc->sata_device, sizeof(pcie_device_t));
}

void sata_get_available_ports(uint64_t cidx, uint8_t* out, int out_size) {
	if (cidx >= controller_count) return;
	sata_controller* ksc = &controllers[cidx];

    for (int i = 0; i < KSATA_MAX_PORTS; i++) {
        if (i + 1 > out_size) break;
        out[i] = ksc->ports_available[i];
    }
}
