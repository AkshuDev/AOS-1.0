#include <aos_inttypes.h>
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
#include <pefi_variables.h>

#include <PBFS/headers/pbfs-fs.h>
#include <PBFS/headers/pbfs.h>

#include <freestanding.h>
#include <ambrc.h>
#include <panic_shell.h>

#include <system.h>
#include <e820.h>

#define MEDIA_DEVICE_PATH 0x04
#define MEDIA_HARDDRIVE_DP 0x01
#define MEDIA_HW_PCI_DP 0x01
#define HARDWARE_DEVICE_PATH 0x01
#define HW_PCI_DP 0x01
#define END_DEVICE_PATH_TYPE 0x7F
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF
#define IS_DEVICE_PATH_END(dp) ((dp) == NULL || ((dp)->Type == END_DEVICE_PATH_TYPE && (dp)->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE))

typedef struct {
	EFI_DEVICE_PATH_PROTOCOL Header;
	UINT8 Function;
	UINT8 Device;
} EFI_PCI_DEVICE_PATH;

static uint8_t current_mode = 0;
static aos_bool rdtscp_supported = AOS_FALSE;
static UINTN uefi_width = 0;
static UINTN uefi_height = 0;

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

EFIAPI static uint64_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint64_t sum = 0;
    for (uint64_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

typedef struct {
    EFI_BLOCK_IO_PROTOCOL *raw;
    EFI_BLOCK_IO_PROTOCOL *partition;
} blockio_pair_t;

__attribute__((noreturn, naked))
EFIAPI static void stage3_jump_to_kernel(void (*kernel)(void), uint64_t stack_top){
    __asm__ __volatile__(
        "movq %0, %%rsp\n\t"
        "movq %0, %%rbp\n\t"
        "cli\n\t"
        "cld\n\t"
		:
		: "r"(stack_top)
		: "memory", "rsp", "rbp"
    );

	__asm__ __volatile__(
		"jmp *%0\n\t"
		:
		: "r"(kernel)
		: "memory"
	);

	__builtin_unreachable();
}

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

EFIAPI void draw_menu_frame(struct VMemDesign* cursor) {
    int width = uefi_width;
    int height = uefi_height;
    int start_x = 0;
    int start_y = 1; // After id

    // Draw Top
    cursor->x = start_x;
    cursor->y = start_y;
    vmem_printwc(cursor, UEFI_BOX_DOUBLE_TOP_RIGHT);
    for(int i = 0; i < width - 2; i++) vmem_printwc(cursor, UEFI_BOX_DOUBLE_HORIZONTAL);
    vmem_printwc(cursor, UEFI_BOX_DOUBLE_TOP_LEFT);

    // Draw Sides
    for(int i = 1; i < height - 2; i++) {
        cursor->x = start_x;
        cursor->y = start_y + i;
        vmem_printwc(cursor, UEFI_BOX_DOUBLE_VERTICAL);
        cursor->x = start_x + width - 1;
        cursor->y = start_y + i;
        vmem_printwc(cursor, UEFI_BOX_DOUBLE_VERTICAL);
    }

    // Draw Bottom
    cursor->x = start_x;
    cursor->y = start_y + height - 2;
    vmem_printwc(cursor, UEFI_BOX_DOUBLE_BOTTOM_RIGHT);
    for(int i = 0; i < width - 2; i++) vmem_printwc(cursor, UEFI_BOX_DOUBLE_HORIZONTAL);
    vmem_printwc(cursor, UEFI_BOX_DOUBLE_BOTTOM_LEFT);

	// Draw id
    const char* id = "AOS Bootloader V1.0";
    int id_x = (uefi_width - strlen(id)) / 2;
    cursor->x = id_x;
    cursor->y = 0;
    vmem_print(cursor, id);
}

EFIAPI void display_splash(struct ambrc* ambrc, struct VMemDesign* cursor) {
    size_t x = (uefi_width/2)-16;
    cursor->x = x;
    cursor->y = (uefi_height/2)-4;
    vmem_print(cursor, "   /$$$$$$   /$$$$$$   /$$$$$$");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, "  /$$__  $$ /$$__  $$ /$$__  $$");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " | $$  \\ $$| $$  \\ $$| $$  \\__/");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " | $$$$$$$$| $$  | $$|  $$$$$$ ");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " | $$__  $$| $$  | $$ \\____  $$");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " | $$  | $$| $$  | $$ /$$  \\ $$");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " | $$  | $$|  $$$$$$/|  $$$$$$/");
    cursor->y++;
    cursor->x = x;
    vmem_print(cursor, " |__/  |__/ \\______/  \\______/ ");

	kdelay((ambrc->display.splash_duration > 10 ? 10 : ambrc->display.splash_duration) * 1000);
}

EFIAPI void display_popup(struct ambrc* ambrc, struct VMemDesign* design, const char* popup_msg) {
    size_t pmsg_len = ALIGN_UP(strlen(popup_msg), 2);
    uint32_t box_w = pmsg_len + 2 > uefi_width ? uefi_width : pmsg_len + 2;
    uint32_t box_h = 12;
    uint32_t start_x = (uefi_width / 2) - (box_w / 2);
    uint32_t start_y = (uefi_height / 2) - (box_h / 2);

    if (pmsg_len > box_w) {
        int x = ((start_x + box_w) / 2) - (pmsg_len / 2);
        int y = start_y + 2;
        char* s = popup_msg;
        int width = 0;
        while (*s) {
            if (width > box_w) {
                x = ((start_x + box_w) / 2) - (pmsg_len / 2);
                y++;
                if (y > box_h) {
                    if (++box_h > uefi_height) return;
                }
                width = 0;
            }
            s++;
            width++;
        }
    }

    design->bg = ambrc->display.bg_color;
    design->fg = ambrc->display.fg_color;
    design->x = start_x;
    design->y = start_y;

	// Draw Top
    vmem_printwc(design, UEFI_BOX_DOUBLE_TOP_RIGHT);
    for(int i = 0; i < box_w - 2; i++) vmem_printwc(design, UEFI_BOX_DOUBLE_HORIZONTAL);
    vmem_printwc(design, UEFI_BOX_DOUBLE_TOP_LEFT);

    // Draw Sides
    for(int i = 1; i < box_h - 1; i++) {
        design->x = start_x;
        design->y = start_y + i;
        vmem_printwc(design, UEFI_BOX_DOUBLE_VERTICAL);

        for(int j = 0; j < box_w - 2; j++) vmem_printc(design, ' ');

        design->x = start_x + box_w - 1;
    	design->y = start_y + i;
        vmem_printwc(design, UEFI_BOX_DOUBLE_VERTICAL);
    }

    // Draw Bottom
    design->x = start_x;
    design->y = start_y + box_h - 1;
    
    vmem_printwc(design, UEFI_BOX_DOUBLE_BOTTOM_RIGHT);
    for(int i = 0; i < box_w - 2; i++) vmem_printwc(design, UEFI_BOX_DOUBLE_HORIZONTAL);
    vmem_printwc(design, UEFI_BOX_DOUBLE_BOTTOM_LEFT);

    if (pmsg_len > box_w - 2) {
        design->x = ((start_x + (box_w / 2)) - (pmsg_len / 2)) + 1;
        design->y = start_y + 2;
        char* s = popup_msg;
        int width = 0;
        while (*s) {
            if (width > box_w - 1) {
                design->x = ((start_x + (box_w / 2)) - (pmsg_len / 2)) + 1;
                design->y++;
                width = 0;
            }
            vmem_printc(design, *s);
            s++;
            width++;
        }
    } else {
        design->x = ((start_x + (box_w / 2)) - (pmsg_len / 2)) + 1;
        design->y = start_y + 2;
        vmem_print(design, popup_msg);
    }
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

EFIAPI static inline uint64_t timer_read_tsc(void) {
    uint32_t low = 0;
    uint32_t high = 0;
    if (rdtscp_supported) {
        asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    }
    else {
        asm volatile("lfence\n\t" "rdtsc" : "=a"(low), "=d"(high) : : "memory");
    }
    return ((uint64_t)high << 32) | low;
}

EFIAPI EFI_STATUS btl_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	uint32_t eax, ebx, ecx, edx;
    eax = 0x80000001;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    if (edx & (1 << 27)) {
        rdtscp_supported = AOS_TRUE;
    } else {
        rdtscp_supported = AOS_FALSE;
    }

	uint64_t tsc_start = timer_read_tsc();
	if (SystemTable == NULL) return ENCODE_ERROR(EFI_LOAD_ERROR);

	if (EFI_ERROR(SystemTable->ConOut->QueryMode(SystemTable->ConOut, (UINTN)SystemTable->ConOut->Mode->Mode, &uefi_width, &uefi_height))) {
		SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Failed to query mode!\r\n");
		return ENCODE_ERROR(EFI_LOAD_ERROR);
	}

	SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Loading PEFI...\r\n");

	SystemTable->BootServices->SetWatchdogTimer(0, 0, 0, NULL);

    InitalizeLib(SystemTable, ImageHandle);
    if (pefi_state.initialized != 1) {
		SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Failed to initialize PEFI!\r\n");
        return ENCODE_ERROR(EFI_LOAD_ERROR);
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
        return ENCODE_ERROR(EFI_LOAD_ERROR);
    }
    g_RBIP = get_physical_disk_io();

    pefi_print(SystemTable, u"Updating SysInfo...\r\n");

    init_backup_ambrc();
    struct ambrc* ambrc = get_ambrc();

    struct VMemDesign cursor = {
        .x = 0,
        .y = 0,
        .fg = ambrc->display.fg_color,
        .bg = ambrc->display.bg_color,
        .serial_out = 0
    };

	EFI_PHYSICAL_ADDRESS SystemInfoPhys = AOS_SYS_INFO_ADDR;
	if (EFI_ERROR(SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, (ALIGN_UP(sizeof(aos_sysinfo_t), 0x1000))/0x1000, &SystemInfoPhys))) {
		vmem_print(&cursor, "Failed to allocate System Information Structure!\n");
		return ENCODE_ERROR(EFI_OUT_OF_RESOURCES);
	}

    ktimer_calibrate();

    uint64_t tsc_start2 = timer_read_tsc();
    kdelay(10);
    uint64_t tsc_end = timer_read_tsc();
    uint64_t cycles_per_ms = (tsc_end - tsc_start2) / 10;

    vmem_disable_cursor();

    vmem_clear_screen(&cursor);
    if (ambrc->display.splash_duration > 0) display_splash(ambrc, &cursor);
    
    struct pbfs_funcs funcs = {
        .malloc=btl_malloc,
        .free=btl_free
    };

    struct pbfs_mount mnt = {0};

	vmem_clear_screen(&cursor);

	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	if (EFI_ERROR(SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage))) {
		vmem_print(&cursor, "Failed to retrieve current drive!\n");
		return ENCODE_ERROR(EFI_LOAD_ERROR);
	}
	EFI_DEVICE_PATH_PROTOCOL* DevicePath;
	if (EFI_ERROR(SystemTable->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID**)&DevicePath))) {
		vmem_print(&cursor, "Failed to retrieve current drive path protocol!\n");
		return ENCODE_ERROR(EFI_LOAD_ERROR);
	}

	EFI_DEVICE_PATH_PROTOCOL* Node = DevicePath;
	struct aos_sysinfo_pcie boot_drive = {0};
	while (!IS_DEVICE_PATH_END(Node)) {
		if (Node->Type == HARDWARE_DEVICE_PATH && Node->SubType == HW_PCI_DP) {
			EFI_PCI_DEVICE_PATH *PciNode = (EFI_PCI_DEVICE_PATH*)Node;

			boot_drive.slot = (uint16_t)PciNode->Device;
			boot_drive.func = (uint16_t)PciNode->Function;
		}

		Node = (EFI_DEVICE_PATH_PROTOCOL*)((uint8_t*)Node + Node->Length[0] + (Node->Length[1] << 8));
	}

	vmem_print(&cursor, "Initializing PBFS...\n");
	struct block_device dev = {0};
	dev.read_block = read_blk;
    dev.write_block = write_blk;
    dev.read = read_f;
    dev.write = write_f;
    dev.flush = flush_f;
    dev.block_size = !g_RBIP ? (uint32_t)g_BIP->Media->BlockSize : (uint32_t)g_RBIP->Media->BlockSize;
    dev.block_count = !g_RBIP ? (uint64_t)(g_BIP->Media->LastBlock + 1) : (uint64_t)(g_RBIP->Media->LastBlock + 1);
    dev.name = "UEFI DISK";

    blockio_pair_t* pair = btl_malloc(sizeof(blockio_pair_t));
    if (!pair) {
        pefi_print(SystemTable, u"Failed to allocate memory!\r\n");
        return ENCODE_ERROR(EFI_OUT_OF_RESOURCES);
    }
    pair->raw = g_RBIP;
	pair->partition = g_BIP;
	dev.driver_data = pair;

	struct drive_device cur_drive = {0};
	cur_drive.active = 0;
	cur_drive.block_dev = dev;
	cur_drive.cur_port = 0;
	cur_drive.name = dev.name;

	pbfs_init(&funcs);
	int out = pbfs_mount(&cur_drive.block_dev, &mnt);
	if (out != PBFS_RES_SUCCESS) {
		vmem_printf(&cursor, "Failed to mount, Error:\n\t%s\n", pbfs_get_err_str(out));
		vmem_printf(&cursor, "Used Drive %s\n\tBlock Count: %llu\n\tBlock Size: %llu\n", cur_drive.block_dev.name, cur_drive.block_dev.block_count, cur_drive.block_dev.block_size);
		return ENCODE_ERROR(EFI_LOAD_ERROR);
	}

    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
	read_blk(&cur_drive.block_dev, mnt.header64.sysinfo_lba, SystemInfo);

	memset(&SystemInfo->fb_info, 0, sizeof(SystemInfo->fb_info));

	if (EFI_ERROR(pefi_init_gop(SystemTable))) {
		SystemInfo->fb_mode = AOS_SYSINFO_FB_MODE_VGA;

		SystemInfo->fb_info.phys_addr = IO_VMEM;
		SystemInfo->fb_info.width = IO_VMEM_MAX_COLS;
		SystemInfo->fb_info.height = IO_VMEM_MAX_ROWS;
	} else {
		SystemInfo->fb_mode = AOS_SYSINFO_FB_MODE_FB;
		SystemInfo->fb_info.phys_addr = (uint64_t)GOP->Mode->FrameBufferBase;
		SystemInfo->fb_info.addr = (uint64_t)GOP->Mode->FrameBufferBase;
		SystemInfo->fb_info.width = GOP->Mode->Info->HorizontalResolution;
		SystemInfo->fb_info.height = GOP->Mode->Info->VerticalResolution;
		SystemInfo->fb_info.bpp = sizeof(uint32_t)*8;
		SystemInfo->fb_info.pitch = GOP->Mode->Info->PixelsPerScanLine * (SystemInfo->fb_info.bpp / 8);
		SystemInfo->fb_info.size = GOP->Mode->FrameBufferSize;

		switch (GOP->Mode->Info->PixelFormat) {
			case PixelRedGreenBlueReserved8BitPerColor:
				// R G B A
				SystemInfo->fb_info.cformat = PYRION_COLORF_RGBA;
				break;

			case PixelBlueGreenRedReserved8BitPerColor:
				// B G R A
				SystemInfo->fb_info.cformat = PYRION_COLORF_BGRA;
				break;

			case PixelBitMask: {
				EFI_PIXEL_BITMASK* mask = &GOP->Mode->Info->PixelInformation;

				if (
					mask->RedMask == 0xFF000000 &&
					mask->GreenMask == 0x00FF0000 &&
					mask->BlueMask == 0x0000FF00
				) {
					SystemInfo->fb_info.cformat = PYRION_COLORF_RGBA;
				} else if (
					mask->RedMask == 0x00FF0000 &&
					mask->GreenMask == 0x0000FF00 &&
					mask->BlueMask == 0x000000FF
				) {
					SystemInfo->fb_info.cformat = PYRION_COLORF_ARGB;
				} else {
					SystemInfo->fb_info.cformat = PYRION_COLORF_BGRA;
				}

				break;
			}

			case PixelBltOnly:
			default:
				SystemInfo->fb_info.cformat = PYRION_COLORF_BGRA;
				break;
		}
	}

    SystemInfo->boot_drive = boot_drive;
	SystemInfo->boot_drive_raw = 0;
    SystemInfo->boot_mode = 0;
    SystemInfo->reserved0 = 0;
    SystemInfo->cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo->cpu_vendor);

	aos_bool tsc_is_fine = 0;
	aos_bool kernel_is_fine = 0;
	switch (ambrc->boot_info.crash_verification_mode) {
        case 0: break;
        case 1: 
            current_mode = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 2 : 0;
            break;
        case 2:
            current_mode = SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG ? 2 : 0;
            break;
        case 3:
            tsc_is_fine = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 0 : 1;
            kernel_is_fine = !(SystemInfo->kernel_info & AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG);
            current_mode = !tsc_is_fine && !kernel_is_fine ? 2 : tsc_is_fine && kernel_is_fine ? 0 : 1;
            break;
        default: break;
    }

    PBFS_Kernel_Entry os_entries[20];
    uint64_t entry_count = 0;
    
    cursor.serial_out = 0;
    vmem_clear_screen(&cursor);
	vmem_disable_cursor();
    draw_menu_frame(&cursor);

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 20, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    int selected = 0;
    int old_selected = -1;
    int scroll_offset = 0;
    int running = 1;
    int ambrc_entry = ambrc->display.show_settings_at_top ? 0 : entry_count;
	int uefi_settings_entry = entry_count+1;
    uint64_t timeout = 0;
    uint64_t total_items = entry_count + 2;

    const int VIEWPORT_HEIGHT = (uefi_height - 6);
    const int MENU_START_Y = 4;

    aos_bool force_redraw = 1;
    aos_bool popup_finished = 0;

    uint64_t timeout_start = kget_ms_passed();

    while (running) {
        if ((ambrc->boot_info.default_os_idx > 0 || ambrc->boot_info.panic_os_idx > 0 || ambrc->boot_info.safe_os_idx > 0) && ambrc->boot_info.timeout != 0) {
            uint64_t timeout_cur = kget_ms_passed();
            uint64_t timeout_elapsed = timeout_cur - timeout_start;
            if ((uint64_t)(timeout_elapsed / cycles_per_ms) > ambrc->boot_info.timeout * 1000) {
                timeout = 1;
                running = 0;
                continue;
            }
        }
        if (current_mode == 1 && !popup_finished) {
			const char* popup_str = "Did anything crash? (y/n/C) (Reason: Unknown)";
            if (!tsc_is_fine && !kernel_is_fine) popup_str = "Did anything crash? (y/n/C) (Reason: TSC and Kernel Both Checks Failed)";
			else if (!tsc_is_fine) popup_str = "Did anything crash? (y/n/C) (Reason: TSC Check Failed)";
			else if (!kernel_is_fine) popup_str = "Did anything crash? (y/n/C) (Reason: Kernel Check Failed)";
			display_popup(ambrc, &cursor, popup_str);
            aos_bool popup_active = 1;
            while (popup_active) {
                UINT16 scancode = read_input(); 
                switch (scancode) {
					case 'y':
                    case 'Y': // Y key
                        current_mode = 2;
                        start_panic_shell(&cur_drive, NULL, 0);
                        popup_active = 0;
                        popup_finished = 1;
                        break;
					case 'n':
                    case 'N': // N key
                        current_mode = 0;
                        popup_active = 0;
                        popup_finished = 1;
                        break;
					case 'c':
                    case 'C': // C Key
                    case 0x17: // ESC Key
                        popup_active = 0;
                        popup_finished = 1;
                        break;
                }
            }
            vmem_clear_screen(&cursor);
            draw_menu_frame(&cursor);
            timeout_start = kget_ms_passed(); // Reset
            force_redraw = 1; 
        }
        if (force_redraw) {
            for (int i = 0; i < VIEWPORT_HEIGHT; i++) {
                int item_idx = scroll_offset + i;
                cursor.x = 1;
                cursor.y = MENU_START_Y + i;
                
                if (item_idx < total_items) {
                    aos_bool is_sel = (item_idx == selected);
                    cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
                    cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

                    const char* name;
                    if (item_idx == ambrc_entry) {
                        name = "AOS Bootloader Settings";
                    } else if (item_idx == uefi_settings_entry) {
                        name = "UEFI Firmware Settings";
                    } else {
                        int k_idx = (ambrc_entry == 0) ? (item_idx - 1) : item_idx;
                        name = os_entries[k_idx].name;
                    }
                    
                    vmem_print(&cursor, name);
                    int len = strlen(name);
                    for (int j = 0; j < (uefi_width - 2 - len); j++) vmem_printc(&cursor, ' ');
                } else {
                    cursor.bg = ambrc->display.bg_color;
                    for (int j = 0; j < (uefi_width - 2); j++) vmem_printc(&cursor, ' ');
                }
            }
            force_redraw = 0;
            old_selected = selected;
			vmem_flush();
        } 
        else if (selected != old_selected) {
            for (int i = 0; i < 2; i++) {
                int target = (i == 0) ? old_selected : selected;
                if (target < scroll_offset || target >= scroll_offset + VIEWPORT_HEIGHT) continue;

                cursor.x = 1;
                cursor.y = MENU_START_Y + (target - scroll_offset);
                aos_bool is_sel = (target == selected);
                cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
                cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

                const char* name;
                if (target == ambrc_entry) {
                    name = "AOS Bootloader Settings";
                } else if (target == uefi_settings_entry) {
					name = "UEFI Firmware Settings";
				} else {
                    int k_idx = (ambrc_entry == 0) ? (target - 1) : target;
                    name = os_entries[k_idx].name;
                }

                vmem_print(&cursor, name);
                int len = strlen(name);
                for (int j = 0; j < (uefi_width - 2 - len); j++) vmem_printc(&cursor, ' ');
            }
            old_selected = selected;
        }

        UINT16 scancode = read_input();
        switch(scancode) {
            case 0x1: // Up
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset--;
                        force_redraw = 1;
                    }
                }
                break;
            case 0x2: // Down
                if (selected < total_items - 1) {
                    selected++;
                    if (selected >= scroll_offset + VIEWPORT_HEIGHT) {
                        scroll_offset++;
                        force_redraw = 1;
                    }
                }
                break;
			case 'S': // Start Shell
				start_panic_shell(&cur_drive, "User Initiated (CAPS-S)", 1);
				vmem_clear_screen(&cursor);
				draw_menu_frame(&cursor);
				timeout_start = kget_ms_passed(); // Reset
				force_redraw = 1;
                break;
            case '\r': // Enter
                if (selected == ambrc_entry) {
                    start_ambrc(&cur_drive);
                    ambrc = get_ambrc();
                    cursor.fg = ambrc->display.fg_color;
                    cursor.bg = ambrc->display.bg_color;
                    ambrc_entry = ambrc->display.show_settings_at_top ? 0 : (int)entry_count;
                    vmem_clear_screen(&cursor);
                    draw_menu_frame(&cursor);
                    force_redraw = 1;
                    timeout_start = kget_ms_passed(); // Reset timeout
                } else if (selected == uefi_settings_entry) {
					UINT64 OsIndications = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
					EFI_STATUS status = SystemTable->RuntimeServices->SetVariable(
						EFI_OS_INDICATIONS_VARIABLE_NAME,
						&gEfiGlobalVariableGuid,
						EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
						sizeof(OsIndications),
						&OsIndications
					);
					if (EFI_ERROR(status)) {
						vmem_clear_screen(&cursor);
						vmem_printf(&cursor, "Failed to Set Variable for UEFI Firmware Settings (Status %llu), doing normal reboot...\n", status);
					}
					SystemTable->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
					return EFI_SUCCESS; // just for safe measure
				} else {
                    running = 0;
                }
                break;
        }
    }

    cursor.fg = ambrc->display.fg_color;
    cursor.bg = ambrc->display.bg_color;
    vmem_clear_screen(&cursor);
    cursor.serial_out = 1;

    int final_kernel_idx = timeout ? 0 : (ambrc_entry == 0) ? selected - 1 : selected;
    if (timeout) {
        switch (current_mode) {
            case 0:
                final_kernel_idx = ambrc->boot_info.default_os_idx - 1;
                break;
            case 1:
                final_kernel_idx = ambrc->boot_info.safe_os_idx - 1;
                break;
            case 2:
                final_kernel_idx = ambrc->boot_info.panic_os_idx - 1;
                break;
            default: break;
        }
    }
    vmem_printf(&cursor, "Loading %s...\n", os_entries[final_kernel_idx].name);

	UINTN pages = ((uint128_to_u64(os_entries[final_kernel_idx].count) * dev.block_size) + 0x1000 - 1) / 0x1000;
	EFI_PHYSICAL_ADDRESS load_addr = (EFI_PHYSICAL_ADDRESS)ambrc->kernel_info[final_kernel_idx].load_addr;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderCode, pages, (EFI_PHYSICAL_ADDRESS*)&load_addr);
	if (EFI_ERROR(status)) {
		vmem_printf(&cursor, "Failed to allocate kernel! : %llu\n", status);
		return status;
	}

    if (!read_f(&cur_drive.block_dev, uint128_to_u64(os_entries[final_kernel_idx].lba), uint128_to_u32(os_entries[final_kernel_idx].count), (void*)ambrc->kernel_info[final_kernel_idx].load_addr)) {
        vmem_print(&cursor, "Failed to read kernel, Disk error!\n");
		return ENCODE_ERROR(EFI_LOAD_ERROR);
    }

	SystemInfo->kernel_info = AOS_BOOTLOADER_KERNEL_ACTIVE_FLAG;
    SystemInfo->checksum = 0;
    SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(aos_sysinfo_t));

	write_blk(&cur_drive.block_dev, mnt.header64.sysinfo_lba, SystemInfo);
	flush_f(&cur_drive.block_dev);
	btl_free(dev.driver_data);

	EFI_PHYSICAL_ADDRESS stack_base = AOS_KERNEL_STACK_TOP - 0x10000; // 64 KB stack
	status = SystemTable->BootServices->AllocatePages(
		AllocateAddress,
		EfiLoaderData,
		16, // 16 pages = 64 KB
		&stack_base
	);

	if (EFI_ERROR(status)) {
		vmem_printf(&cursor, "Failed to allocate kernel stack! : %llu\n", status);
		return status;
	}

	vmem_print(&cursor, "Getting Map...\n");

	UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;

    status = SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (status != ENCODE_ERROR(EFI_BUFFER_TOO_SMALL)) return status;

	vmem_print(&cursor, "Allocating memory pool for map...\n");
	while (status == ENCODE_ERROR(EFI_BUFFER_TOO_SMALL)) {
    	map_size += desc_size * 2;
		if (map) SystemTable->BootServices->FreePool(map);
		if (EFI_ERROR(SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID**)&map)))
			return ENCODE_ERROR(EFI_OUT_OF_RESOURCES);
		status = SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
	}
	if (EFI_ERROR(status)) {
		vmem_printf(&cursor, "Failed to allocate memory pool! : %llu\n", status);
		return status;
	}

	vmem_print(&cursor, "Initializing E820 Map...\n");
	EFI_PHYSICAL_ADDRESS E820MapPhys = AOS_E820_INFO_ADDR;
	if (EFI_ERROR(SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, (ALIGN_UP(sizeof(struct bs1_e820) + (sizeof(struct bs1_e820_entry)*(map_size / desc_size)), 0x1000))/0x1000, &E820MapPhys))) {
		vmem_print(&cursor, "Failed to allocate E820 Map Structure!\n");
		return ENCODE_ERROR(EFI_OUT_OF_RESOURCES);
	}
	struct bs1_e820* e820_m = (struct bs1_e820*)AOS_E820_INFO_ADDR;
	struct bs1_e820_entry* e820 = (struct bs1_e820_entry*)e820_m->entries;
	uint32_t e820_count = 0;

	struct bs1_e820_entry* last_entry = NULL;
	for (UINTN off = 0; off < map_size; off += desc_size) {
		if (e820_count >= E820_MAX_ENT) break;
		EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + off);
		struct bs1_e820_entry* e = &e820[e820_count];
		e->base = desc->PhysicalStart;
		e->len = desc->NumberOfPages * 0x1000ULL;
		switch (desc->Type) {
			case EfiConventionalMemory:
			case EfiLoaderCode:
			case EfiLoaderData:
			case EfiBootServicesCode:
			case EfiBootServicesData:
			case EfiPersistentMemory:
				e->type = E820_TYPE_RAM;
				break;

			case EfiACPIReclaimMemory:
				e->type = E820_TYPE_ACPI_RECLAIM;
				break;

			case EfiACPIMemoryNVS:
				e->type = E820_TYPE_ACPI_NVS;
				break;

			case EfiUnusableMemory:
				e->type = E820_TYPE_BAD;
				break;

			default:
				e->type = E820_TYPE_RESERVED;
				break;
		}
		e->ext = 1;

		if (last_entry) {
			if (
				e->base == (last_entry->base + last_entry->len) &&
				e->type == last_entry->type &&
				e->ext == last_entry->ext
			) {
				last_entry->len += e->len;
			} else {
				last_entry = e;
				e820_count++;
			}
		} else {
			last_entry = e;
			e820_count++;
		}
	}

	e820_m->entry_count = e820_count;

	vmem_print(&cursor, "Jumping to Kernel...\n");
	vmem_flush();
	status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
	while (status == ENCODE_ERROR(EFI_INVALID_PARAMETER)) {
		EFI_STATUS status2 = SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
		while (status2 == ENCODE_ERROR(EFI_BUFFER_TOO_SMALL)) {
			map_size += desc_size * 2;
			if (map) SystemTable->BootServices->FreePool(map);
			if (EFI_ERROR(SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID**)&map)))
				return ENCODE_ERROR(EFI_OUT_OF_RESOURCES);
			status2 = SystemTable->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
		}
		if (EFI_ERROR(status2)) {
			vmem_printf(&cursor, "Failed to allocate memory pool! : %llu\n", status2);
			return status2;
		}
		status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
		if (EFI_ERROR(status) && status != ENCODE_ERROR(EFI_INVALID_PARAMETER)) {
			return status;
		}
	}

	stage3_jump_to_kernel((void(*)(void))((void*)ambrc->kernel_info[final_kernel_idx].entry_point), AOS_KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

EFIAPI EFI_STATUS efi_main(EFI_HANDLE h, EFI_SYSTEM_TABLE *st) {
    return btl_main(h, st);
}
