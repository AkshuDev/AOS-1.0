#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <pefi_types.h>
#include <pefi_bootservices.h>
#include <pefi_loaded_image.h>
#include <pefi_disk.h>
#include <pefi_memory.h>
#include <pefi_main.h>
#include <pefi_error.h>
#include <pefi_simple_text_in.h>
#include <pefi_priv.h>
#include <pefi.h>
#include <pefi_block_io.h>
#include <pefi_init.h>

#include <PBFS/headers/pbfs-fs.h>
#include <PBFS/headers/pbfs.h>

#include <freestanding.h>
#include <ambrc.h>

#include <system.h>

#define MEDIA_DEVICE_PATH 0x04
#define MEDIA_HARDDRIVE_DP 0x01
#define END_DEVICE_PATH_TYPE 0x7F
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF
#define BOXDRAW_HORIZONTAL u"\u2500"
#define BOXDRAW_VERTICAL u"\u2502"
#define BOXDRAW_DOWN_RIGHT u"\u250C"
#define BOXDRAW_DOWN_LEFT u"\u2510"
#define BOXDRAW_UP_RIGHT u"\u2514"
#define BOXDRAW_UP_LEFT u"\u2518"
#define IS_DEVICE_PATH_END(dp) ((dp) == NULL || ((dp)->Type == END_DEVICE_PATH_TYPE && (dp)->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE))

EFIAPI static void cpuid_get_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    asm volatile(
		"cpuid"
		: "+a"(eax),
		"=b"(ebx),
		"=c"(ecx),
		"=d"(edx)
	);
    *((uint32_t*)vendor_out) = ebx;
    *((uint32_t*)(vendor_out + 4)) = edx;
    *((uint32_t*)(vendor_out + 8)) = ecx;
    vendor_out[12] = 0;
}

EFIAPI static uint32_t cpuid_signature(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile (
        "pushq %%rbx\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "popq %%rbx"
        : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
        : "cc"
    );
    return eax;
}

EFIAPI static uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

typedef struct {
    EFI_BLOCK_IO_PROTOCOL *raw;
    EFI_BLOCK_IO_PROTOCOL *partition;
} blockio_pair_t;

__attribute__((noreturn))
EFIAPI static void stage3_jump_to_kernel(void (*kernel)(void), uint64_t stack_top) {
    __asm__ __volatile__ (
        "mov %rdx, %rsi\n\t" // Move stack_top to RSI
        "mov %rcx, %rdi\n\t" // Move kernel pointer to RDI
        "mov %rsi, %rsp\n\t" // Set the stack pointer
        "mov %rsi, %rbp\n\t"
        "cli\n\t"
        "cld\n\t"
        "jmp *%rdi\n\t" // Jump to kernel
    );
}

static uint8_t check_apic(void) { return 0; } // Stub for now
static uint64_t measure_tsc(void) { return 0; } // Stub for now

EFIAPI static void* btl_malloc(size_t size) {
    if (pefi_state.initialized != 1) return NULL;

    void* ptr = NULL;
    int status = pefi_allocate(pefi_state.system_table, size, &ptr);
    if (EFI_ERROR(status)) return NULL;
    return ptr;
}
EFIAPI static void btl_free(void* ptr) {
    if (pefi_state.initialized != 1) return;
    pefi_free(pefi_state.system_table, ptr);
}

EFIAPI static int read_blk(struct block_device* dev, uint64_t lba, void* buf) {
    if (pefi_state.initialized != 1) return 0;
	blockio_pair_t *pair = dev->driver_data;
    int status = pefi_read_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, 1, buf, pair->raw ? pair->raw : pair->partition);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_blk(struct block_device* dev, uint64_t lba, const void* buf) {
    if (pefi_state.initialized != 1) return 0;
	blockio_pair_t *pair = dev->driver_data;
    int status = pefi_write_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, 1, buf, pair->raw ? pair->raw : pair->partition);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int read_f(struct block_device* dev, uint64_t lba, uint64_t count, void* buf) {
    if (pefi_state.initialized != 1) return 0;
	blockio_pair_t *pair = dev->driver_data;
    int status = pefi_read_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, (UINTN)count, buf, pair->raw ? pair->raw : pair->partition);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_f(struct block_device* dev, uint64_t lba, uint64_t count, const void* buf) {
    if (pefi_state.initialized != 1) return 0;
	blockio_pair_t *pair = dev->driver_data;
    int status = pefi_write_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, (UINTN)count, buf, pair->raw ? pair->raw : pair->partition);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int flush_f(struct block_device* dev) {
    if (pefi_state.initialized != 1) return 0;
	blockio_pair_t *pair = dev->driver_data;
    EFI_BLOCK_IO_PROTOCOL* BIP = pair->raw ? pair->raw : pair->partition;
    int status = BIP->FlushBlocks(BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}

EFIAPI static bool draw_menu_frame(void) {
    if (pefi_state.initialized != 1) return false;

    UINTN start_x = 0;
    UINTN start_y = 0;
    UINTN width = 0;
    UINTN height = 0;

    pefi_state.system_table->ConOut->QueryMode(pefi_state.system_table->ConOut, pefi_state.system_table->ConOut->Mode->Mode, &width, &height);

    // Draw Top
    pefi_state.system_table->ConOut->ClearScreen(pefi_state.system_table->ConOut);
    pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x, start_y);

    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_UP_RIGHT);
    for (UINTN i = 0; i < width - 2; i++) {
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_HORIZONTAL);
    }
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_DOWN_RIGHT);

    // Draw Sides
    for(UINTN i = 1; i < height - 1; i++) {
        pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x, start_y + i);
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_VERTICAL);
        pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x + width - 1, start_y + i);
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_VERTICAL);
    }

    // Draw Bottom
    pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x, start_y + height - 1);
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_DOWN_RIGHT);
    for (UINTN i = 0; i < width - 2; i++) {
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_HORIZONTAL);
    }
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, BOXDRAW_DOWN_LEFT);

    return true;
}

EFIAPI static void char_to_char16(char* s, CHAR16* out, size_t out_size) {
    size_t i = 0;

    for (; s[i] && i < out_size - 1; i++)
        out[i] = (CHAR16)s[i];

    out[i] = 0;
}

EFIAPI static UINT16 read_input(void) {
    if (pefi_state.initialized != 1) return 0;
    EFI_INPUT_KEY key;
    EFI_STATUS status;

    while (1) {
        status = pefi_state.system_table->ConIn->ReadKeyStroke(pefi_state.system_table->ConIn, &key);
        if (!EFI_ERROR(status)) {
            return key.UnicodeChar ? key.UnicodeChar : key.ScanCode;
        }
    }
}

EFIAPI static bool is_partition_device_path(EFI_DEVICE_PATH_PROTOCOL* dp) {
	if (!dp) return false;
    while (!IS_DEVICE_PATH_END(dp)) {
        if (dp->Type == MEDIA_DEVICE_PATH) {
            if (dp->SubType == MEDIA_HARDDRIVE_DP) {
                return true;
            }
        }

        dp = (EFI_DEVICE_PATH_PROTOCOL*)((uint8_t*)dp + dp->Length[0] + (dp->Length[1] << 8));
    }

    return false;
}

EFIAPI static EFI_BLOCK_IO_PROTOCOL* get_physical_disk_io(void) {
    if (pefi_state.initialized != 1) return NULL;

	pefi_print(pefi_state.system_table, u"Finding Physical Disk : ");

    UINTN handle_count = 0;
    EFI_HANDLE* handles = NULL;
    EFI_STATUS status = pefi_state.boot_services->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &handle_count, &handles);
    if (EFI_ERROR(status)) {
		pefi_print(pefi_state.system_table, u"Failed to retrieve Physical Disk (LocateHandleBuffer) !\r\n");
		return NULL;
	}

	EFI_BLOCK_IO_PROTOCOL* best = NULL;

    for (UINTN i = 0; i < handle_count; i++) {
        EFI_BLOCK_IO_PROTOCOL* bio = NULL;
        if (EFI_ERROR(pefi_state.boot_services->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (VOID**)&bio)))
            continue;
		if (!bio) continue;

        EFI_DEVICE_PATH_PROTOCOL* dp = NULL;
		if (EFI_ERROR(pefi_state.boot_services->HandleProtocol(handles[i], &gEfiDevicePathProtocolGuid, (VOID**)&dp)))
			continue;

		if (IS_DEVICE_PATH_END(dp) || is_partition_device_path(dp)) continue;

		if (!best || bio->Media->LastBlock > best->Media->LastBlock) {
            best = bio;
        }
    }

	if (handles) pefi_state.boot_services->FreePool(handles);

	if (best) pefi_print(pefi_state.system_table, u"Got Physical Disk!\r\n");
	else pefi_print(pefi_state.system_table, u"Failed to retrieve Physical Disk!\r\n");
    return best;
}

EFIAPI EFI_STATUS btl_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    if (SystemTable == NULL) return EFI_LOAD_ERROR;

	SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Loading PEFI...\r\n");

    InitalizeLib(SystemTable, ImageHandle);
    if (pefi_state.initialized != 1) {
		SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Failed to initialize PEFI!\r\n");
        return EFI_LOAD_ERROR;
    }

	SystemTable->ConOut->OutputString(SystemTable->ConOut, u"PEFI Loaded\r\n");

    pefi_clear(SystemTable);

    pefi_print(SystemTable, u"Welcome to AOS Bootloader!\r\n");
    pefi_print(SystemTable, u"Getting Block IOs...\r\n");
    EFI_BLOCK_IO_PROTOCOL* g_BIP = NULL;
    EFI_BLOCK_IO_PROTOCOL* g_RBIP = NULL;

    g_BIP = pefi_find_block_io(SystemTable);
    if (g_BIP == NULL) {
        pefi_print(SystemTable, u"Failed to find Block IO!\r\n");
        return EFI_LOAD_ERROR;
    }
    g_RBIP = get_physical_disk_io();
    pefi_print(SystemTable, u"Updating SysInfo...\r\n");
    
    EFI_PHYSICAL_ADDRESS sysinfo_addr = AOS_SYS_INFO_ADDR;
    if (EFI_ERROR(SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, 1, &sysinfo_addr))) {
        pefi_print(SystemTable, u"Failed to allocate memory for SystemInfo structure!\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)(UINTN)sysinfo_addr;
    SystemInfo->boot_drive = 0x0;
    SystemInfo->boot_mode = 0;
    SystemInfo->reserved0 = 0;
    SystemInfo->cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo->cpu_vendor);
    SystemInfo->apic_present = check_apic();
    SystemInfo->tsc_freq_hz = measure_tsc();
	SystemInfo->checksum = 0;
    SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(aos_sysinfo_t));

	init_backup_ambrc();
    struct ambrc* ambrc = get_ambrc();

    struct pbfs_funcs funcs = {
        .malloc=btl_malloc,
        .free=btl_free
    };

    struct pbfs_mount mnt = {0};
    struct block_device dev = {0};

    pefi_print(SystemTable, u"Initializing PBFS...\r\n");
    dev.read_block = read_blk;
    dev.write_block = write_blk;
    dev.read = read_f;
    dev.write = write_f;
    dev.flush = flush_f;
    dev.block_size = !g_RBIP ? (uint32_t)g_BIP->Media->BlockSize : (uint32_t)g_RBIP->Media->BlockSize;
    dev.block_count = !g_RBIP ? (uint64_t)(g_BIP->Media->LastBlock + 1) : (uint64_t)(g_RBIP->Media->LastBlock + 1);
    dev.name = "UEFI DISK";

	uefi_printf("Disk Name: %s\r\n\tBlock Size: %u\r\n\tBlock Count: %llu\r\n", dev.name, dev.block_size, dev.block_count);

    blockio_pair_t* pair = btl_malloc(sizeof(blockio_pair_t));
    if (!pair) {
        pefi_print(SystemTable, u"Failed to allocate memory!\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    pair->raw = g_RBIP;
	pair->partition = g_BIP;
	dev.driver_data = pair;

    pbfs_init(&funcs);
    
    int out = pbfs_mount(&dev, &mnt);
    if (out != PBFS_RES_SUCCESS) {
        pefi_print(SystemTable, u"Failed to mount, Error:\r\n\t");
        CHAR16 err[128];
        char_to_char16(pbfs_get_err_str(out), err, 128);
        pefi_print(SystemTable, err);
        return EFI_DEVICE_ERROR;
    }

    PBFS_Kernel_Entry os_entries[30];
    uint64_t entry_count = 0;
    pefi_print(SystemTable, u"Full initialization complete!\r\n");

    if (!draw_menu_frame()) {
        pefi_print(SystemTable, u"Failed to draw menu frame!\r\n");
        return EFI_LOAD_ERROR;
    }

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 17, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    UINTN width = 0;
    UINTN height = 0;

    SystemTable->ConOut->QueryMode(SystemTable->ConOut, SystemTable->ConOut->Mode->Mode, &width, &height);

    int selected = 0;
    int running = 1;
    CHAR16* help_text = u"Use UP/DOWN to select, ENTER to boot";
    size_t help_text_len = 36;
    size_t help_text_x = (width - help_text_len) / 2;
    uint64_t entry_count_fit = entry_count + 1 > (height - 4) ? (height - 4) : entry_count + 1;

	int back_to_uefi_settings_idx = entry_count;

    CHAR16* empty = btl_malloc((width - 1) * sizeof(CHAR16));
    for(UINTN i = 0; i < width-2; i++)
        empty[i] = L' ';
    empty[width-2] = 0;

	uint8_t current_mode = 0;
	// switch (ambrc->boot_info.crash_verification_mode) {
    //     case 0: break;
    //     case 1: 
    //     current_mode = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 2 : 0;
    //         break;
    //     case 2:
    //         current_mode = SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG ? 2 : 0;
    //         break;
    //     case 3:
    //         uint8_t tsc_is_fine = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 0 : 1;
    //         uint8_t kernel_is_fine = !(SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG);
    //         current_mode = !tsc_is_fine && !kernel_is_fine ? 2 : tsc_is_fine && kernel_is_fine ? 0 : 1;
    //         break;
    //     default: break;
    // }

    while (running) {
        for (int i = 0; i < entry_count_fit; i++) {
            SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 1, 4 + i);
            if(i == selected) {
                SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x00 | (0x0F << 4));
            } else {
                SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
            }

            if(i < entry_count_fit - 1) {
                uefi_printf("%s", os_entries[i].name);
            } else if (i == entry_count_fit - 1) {
                pefi_print(SystemTable, u"UEFI Firmware Settings");
            } else {
                pefi_print(SystemTable, empty);
            }
        }

        SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
        SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, help_text_x, 1);
        pefi_print(SystemTable, help_text);

        UINT16 sc = read_input();
        if (sc == 0x0001 && selected > 0) {
            selected--;
        } else if (sc == 0x0002 && selected < entry_count_fit-1) {
            selected++;
        } else if (sc == '\r') {
            running = 0;
        }
    }

    if (selected == back_to_uefi_settings_idx) {
        SystemTable->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    }

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
    
	uefi_printf("Selected : %d Entry Count : %d Entry Count Fit: %d ", selected, entry_count, entry_count_fit);

    pefi_print(SystemTable, u"Loading ");
    CHAR16 nm[128];
    char_to_char16(os_entries[selected].name, nm, 128);
    pefi_print(SystemTable, nm);
    pefi_print(SystemTable, u"...\r\n");

    UINTN pages = ((uint128_to_u64(os_entries[selected].count) * dev.block_size) + 0x1000 - 1) / 0x1000;
    SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderCode, pages, (EFI_PHYSICAL_ADDRESS*)AOS_KERNEL_LOC);

    if (read_f(&dev, uint128_to_u64(os_entries[selected].lba), uint128_to_u64(os_entries[selected].count), AOS_KERNEL_LOC) != 1) {
        pefi_print(SystemTable, u"Failed to read kernel!\r\n");
        return EFI_LOAD_ERROR;
    }

    pefi_print(SystemTable, u"Jumping to kernel...\r\n");
    btl_free(dev.driver_data);
    btl_free(empty);

    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;

    int status = SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    map_size += desc_size * 2;
    SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID**)&map);
    SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);

    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    pefi_print(SystemTable, u"Failed to start Kernel!\r\n");
    return EFI_LOAD_ERROR;
}

// void stage3(void) {
//     init_backup_ambrc();
//     struct ambrc* ambrc = get_ambrc();

//     struct VMemDesign cursor = {
//         .x = 0,
//         .y = 0,
//         .fg = ambrc->display.fg_color,
//         .bg = ambrc->display.bg_color,
//         .serial_out = 0
//     };

// 	pager_init();
//     acpi_init();
// 	modules_init();
// 	pcie_init();

//     ktimer_calibrate();

//     uint64_t tsc_start2 = read_tsc();
//     kdelay(1);
//     uint64_t tsc_end = read_tsc();
//     uint64_t cycles_per_ms = tsc_end - tsc_start2;

//     vmem_disable_cursor();

//     vmem_clear_screen(&cursor);
//     if (ambrc->display.splash_duration > 0) display_splash(ambrc, &cursor);

// 	unsigned char *mem = (unsigned char *)0x100000;
//     *mem = 0xAA;
//     if (*mem != 0xAA) {
//         cursor.fg = ambrc->display.error_fg_color;
//         vmem_print(&cursor, "A20 line disabled!\n");
//         for (;;) asm("hlt");
//     }
    
//     struct pbfs_funcs funcs = {
//         .malloc=btl_malloc,
//         .free=btl_free
//     };

//     struct pbfs_mount mnt = {0};

// 	vmem_clear_screen(&cursor);

// 	uint8_t boot_drive = *AOS_BOOT_INFO_LOC; // Get Boot drive

//     if (get_available_drives(&cur_drive) == 1) {
//         if (cur_drive.active != 1) {
//             if (cur_drive.init != NULL) cur_drive.init(NULL);
//         }
//         if (cur_drive.read_blk == NULL) {
//             vmem_print(&cursor, "No read function for drive?\n");
//         } else {
//             found_drive = 1;
//         }
//     }

//     if (found_drive != 1) {
// 		serial_print("Trying to find Legacy-ATA...\n");
// 		if (ata_exists() == 0) {
//         	serial_print("No supported drive found!\n");
// 			start_panic_shell(&cur_drive, "No supported drive found!", 1);
//         	acpi_reboot();
// 		} else {
// 			found_drive = 1;
// 			cur_drive.read_blk = ata_read;
// 			cur_drive.write_blk = ata_write;
// 			cur_drive.flush = ata_flush;
// 			cur_drive.cur_port = boot_drive;
// 			ata_identity_t iden;
// 			if (ata_identify_device(boot_drive, &iden) != 1) {
// 				serial_print("Failed to identify Legacy-ATA Drive\n");
//         		start_panic_shell(&cur_drive, "Failed to identify Legacy-ATA Drive", 1);
//         		acpi_reboot();
// 			}

// 			cur_drive.block_dev.block_count = iden.block_count;
// 			cur_drive.block_dev.block_size = iden.block_size;
// 		}
//     }

// 	vmem_print(&cursor, "Initializing PBFS...\n");
// 	cur_drive.block_dev.read_block = read_blk;
// 	cur_drive.block_dev.write_block = write_blk;
// 	cur_drive.block_dev.read = read_f;
// 	cur_drive.block_dev.write = write_f;
// 	cur_drive.block_dev.flush = flush_f;
// 	pbfs_init(&funcs);
// 	int out = pbfs_mount(&cur_drive.block_dev, &mnt);
// 	if (out != PBFS_RES_SUCCESS) {
// 		serial_printf("Failed to mount, Error:\n\t%s\n", pbfs_get_err_str(out));
// 		start_panic_shell(&cur_drive, (const char*)pbfs_get_err_str(out), 1);
// 		acpi_reboot();
// 	}

//     aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
// 	read_blk(&cur_drive.block_dev, mnt.header64.sysinfo_lba, SystemInfo);
//     SystemInfo->boot_drive = boot_drive;
//     SystemInfo->boot_mode = 0;
//     SystemInfo->reserved0 = 0;
//     SystemInfo->cpu_signature = cpuid_signature();
//     cpuid_get_vendor(SystemInfo->cpu_vendor);
//     SystemInfo->checksum = 0;
//     SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(aos_sysinfo_t));

// 	switch (ambrc->boot_info.crash_verification_mode) {
//         case 0: break;
//         case 1: 
//             current_mode = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 2 : 0;
//             break;
//         case 2:
//             current_mode = SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG ? 2 : 0;
//             break;
//         case 3:
//             uint8_t tsc_is_fine = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 0 : 1;
//             uint8_t kernel_is_fine = !(SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG);
//             current_mode = !tsc_is_fine && !kernel_is_fine ? 2 : tsc_is_fine && kernel_is_fine ? 0 : 1;
//             break;
//         default: break;
//     }

//     PBFS_Kernel_Entry os_entries[20];
//     uint64_t entry_count = 0;
    
//     cursor.serial_out = 0;
//     vmem_clear_screen(&cursor);
//     draw_menu_frame(&cursor);

//     if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 20, &entry_count) != PBFS_RES_SUCCESS) {
//         entry_count = 0;
//     }

//     int selected = 0;
//     int old_selected = -1;
//     int scroll_offset = 0;
//     int running = 1;
//     int ambrc_entry = ambrc->display.show_settings_at_top ? 0 : entry_count;
//     uint64_t timeout = 0;
//     uint64_t total_items = entry_count + 1;

//     const int VIEWPORT_HEIGHT = (IO_VMEM_MAX_ROWS - 6);
//     const int MENU_START_Y = 4;

//     uint8_t force_redraw = 1;
//     uint8_t popup_finished = 0;

//     uint64_t timeout_start = read_tsc();

//     while (running) {
//         if ((ambrc->boot_info.default_os_idx > 0 || ambrc->boot_info.panic_os_idx > 0 || ambrc->boot_info.safe_os_idx > 0) && ambrc->boot_info.timeout != 0) {
//             uint64_t timeout_cur = read_tsc();
//             uint64_t timeout_elapsed = timeout_cur - timeout_start;
//             if ((uint64_t)(timeout_elapsed / cycles_per_ms) > ambrc->boot_info.timeout * 1000) {
//                 timeout = 1;
//                 running = 0;
//                 continue;
//             }
//         }
//         if (current_mode == 1 && !popup_finished) {
//             display_popup(ambrc, &cursor, "Did anything crash? (y/n/C)");
//             uint8_t popup_active = 1;
//             while (popup_active) {
//                 uint8_t scancode = ps2_read_scan(); 
//                 switch (scancode) {
//                     case 0x15: // Y key
//                         current_mode = 2;
//                         start_panic_shell(&cur_drive, NULL, 0);
//                         popup_active = 0;
//                         popup_finished = 1;
//                         break;
//                     case 0x31: // N key
//                         current_mode = 0;
//                         popup_active = 0;
//                         popup_finished = 1;
//                         break;
//                     case 0x2E: // C Key
//                     case 0x01: // ESC Key
//                         popup_active = 0;
//                         popup_finished = 1;
//                         break;
//                 }
//             }
//             vmem_clear_screen(&cursor);
//             draw_menu_frame(&cursor);
//             timeout_start = read_tsc(); // Reset
//             force_redraw = 1; 
//         }
//         if (force_redraw) {
//             for (int i = 0; i < VIEWPORT_HEIGHT; i++) {
//                 int item_idx = scroll_offset + i;
//                 cursor.x = 1;
//                 cursor.y = MENU_START_Y + i;
                
//                 if (item_idx < total_items) {
//                     uint8_t is_sel = (item_idx == selected);
//                     cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
//                     cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

//                     const char* name;
//                     if (item_idx == ambrc_entry) {
//                         name = "AOS Bootloader Settings";
//                     } else {
//                         int k_idx = (ambrc_entry == 0) ? (item_idx - 1) : item_idx;
//                         name = os_entries[k_idx].name;
//                     }
                    
//                     vmem_print(&cursor, name);
//                     int len = strlen(name);
//                     for (int j = 0; j < (IO_VMEM_MAX_COLS - 2 - len); j++) vmem_printc(&cursor, ' ');
//                 } else {
//                     cursor.bg = ambrc->display.bg_color;
//                     for (int j = 0; j < (IO_VMEM_MAX_COLS - 2); j++) vmem_printc(&cursor, ' ');
//                 }
//             }
//             force_redraw = 0;
//             old_selected = selected;
//         } 
//         else if (selected != old_selected) {
//             for (int i = 0; i < 2; i++) {
//                 int target = (i == 0) ? old_selected : selected;
//                 if (target < scroll_offset || target >= scroll_offset + VIEWPORT_HEIGHT) continue;

//                 cursor.x = 1;
//                 cursor.y = MENU_START_Y + (target - scroll_offset);
//                 uint8_t is_sel = (target == selected);
//                 cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
//                 cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

//                 const char* name;
//                 if (target == ambrc_entry) {
//                     name = "AOS Bootloader Settings";
//                 } else {
//                     int k_idx = (ambrc_entry == 0) ? (target - 1) : target;
//                     name = os_entries[k_idx].name;
//                 }

//                 vmem_print(&cursor, name);
//                 int len = strlen(name);
//                 for (int j = 0; j < (IO_VMEM_MAX_COLS - 2 - len); j++) vmem_printc(&cursor, ' ');
//             }
//             old_selected = selected;
//         }

//         uint8_t scancode = ps2_try_read_scan();
//         switch(scancode) {
//             case 0x48: // Up
//                 if (selected > 0) {
//                     selected--;
//                     if (selected < scroll_offset) {
//                         scroll_offset--;
//                         force_redraw = 1;
//                     }
//                 }
//                 break;
//             case 0x50: // Down
//                 if (selected < total_items - 1) {
//                     selected++;
//                     if (selected >= scroll_offset + VIEWPORT_HEIGHT) {
//                         scroll_offset++;
//                         force_redraw = 1;
//                     }
//                 }
//                 break;
//             case 0x1C: // Enter
//                 if (selected == ambrc_entry) {
//                     start_ambrc(&cur_drive);
//                     ambrc = get_ambrc();
//                     cursor.fg = ambrc->display.fg_color;
//                     cursor.bg = ambrc->display.bg_color;
//                     ambrc_entry = ambrc->display.show_settings_at_top ? 0 : (int)entry_count;
//                     vmem_clear_screen(&cursor);
//                     draw_menu_frame(&cursor);
//                     force_redraw = 1;
//                     timeout_start = read_tsc(); // Reset timeout
//                 } else {
//                     running = 0;
//                 }
//                 break;
//         }
//     }

//     cursor.fg = ambrc->display.fg_color;
//     cursor.bg = ambrc->display.bg_color;
//     vmem_clear_screen(&cursor);
//     cursor.serial_out = 1;

//     int final_kernel_idx = timeout ? 0 : (ambrc_entry == 0) ? selected - 1 : selected;
//     if (timeout) {
//         switch (current_mode) {
//             case 0:
//                 final_kernel_idx = ambrc->boot_info.default_os_idx - 1;
//                 break;
//             case 1:
//                 final_kernel_idx = ambrc->boot_info.safe_os_idx - 1;
//                 break;
//             case 2:
//                 final_kernel_idx = ambrc->boot_info.panic_os_idx - 1;
//                 break;
//             default: break;
//         }
//     }
//     vmem_printf(&cursor, "Loading %s...\n", os_entries[final_kernel_idx].name);

// 	pager_map_range((uint64_t)ambrc->kernel_info[final_kernel_idx].load_addr, (uint64_t)ambrc->kernel_info[final_kernel_idx].load_addr, (mnt.header64.block_size * uint128_to_u64(os_entries[final_kernel_idx].count)) + 0x40000000, PAGE_PRESENT | PAGE_RW);
	
//     if (!read_f(&cur_drive.block_dev, uint128_to_u64(os_entries[final_kernel_idx].lba), uint128_to_u32(os_entries[final_kernel_idx].count), (void*)ambrc->kernel_info[final_kernel_idx].load_addr)) {
//         serial_print("Failed to read kernel, Disk error!\n");
// 		start_panic_shell(&cur_drive, "Failed to read kernel, Disk error!", 1);
//         acpi_reboot();
//     }

// 	SystemInfo->kernel_info = AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG;
// 	write_blk(&cur_drive.block_dev, mnt.header64.sysinfo_lba, SystemInfo);

//     vmem_print(&cursor, "Jumping to Kernel...\n");
// 	stage3_jump_to_kernel((void(*)(void))((void*)ambrc->kernel_info[final_kernel_idx].entry_point), AOS_KERNEL_STACK_TOP);

//     __builtin_unreachable(); // Tell GCC control never returns 
// }


EFIAPI EFI_STATUS efi_main(EFI_HANDLE h, EFI_SYSTEM_TABLE *st) {
    return btl_main(h, st);
}
