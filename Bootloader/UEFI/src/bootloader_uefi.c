#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <pefilib.h>
#include <pefi_bootservices.h>
#include <pefi_loaded_image.h>
#include <pefi_disk.h>
#include <pefi_simple_text_in.h>

#include <PBFS/headers/pbfs-fs.h>
#include <PBFS/headers/pbfs.h>

#include <system.h>

EFIAPI static void cpuid_get_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    __asm__ volatile ("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax));
    *((uint32_t*)vendor_out) = ebx;
    *((uint32_t*)(vendor_out + 4)) = edx;
    *((uint32_t*)(vendor_out + 8)) = ecx;
    vendor_out[12] = 0;
}

EFIAPI static uint32_t cpuid_signature(void) {
    uint32_t eax;
    __asm__ volatile ("cpuid"
        : "=a"(eax)
        : "a"(1)
        : "ebx", "ecx", "edx");
    return eax;
}

EFIAPI static uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

__attribute__((naked, noreturn))
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

static EFI_SYSTEM_TABLE* g_ST = NULL;
static EFI_BOOT_SERVICES* g_BS = NULL;
static EFI_HANDLE g_IH = 0;
static EFI_BLOCK_IO_PROTOCOL* g_BIP = NULL;
static EFI_BLOCK_IO_PROTOCOL* g_RBIP = NULL;

EFIAPI static void* btl_malloc(size_t size) {
    if (!g_BS) return NULL;

    void* ptr = NULL;
    int status = pefi_allocate(g_ST, size, &ptr);
    if (EFI_ERROR(status)) return NULL;
    return ptr;
}
EFIAPI static void btl_free(void* ptr) {
    if (!g_BS) return;
    g_BS->FreePool(ptr);
}

EFIAPI static int read_blk(struct block_device* dev, uint64_t lba, void* buf) {
    int status = pefi_read_lba(g_ST, g_IH, (EFI_LBA)lba, 1, buf, g_RBIP ? g_RBIP : g_BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_blk(struct block_device* dev, uint64_t lba, const void* buf) {
    int status = pefi_write_lba(g_ST, g_IH, (EFI_LBA)lba, 1, buf, g_RBIP ? g_RBIP : g_BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int read_f(struct block_device* dev, uint64_t lba, uint64_t count, void* buf) {
    int status = pefi_read_lba(g_ST, g_IH, (EFI_LBA)lba, (UINTN)count, buf, g_RBIP ? g_RBIP : g_BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_f(struct block_device* dev, uint64_t lba, uint64_t count, const void* buf) {
    int status = pefi_write_lba(g_ST, g_IH, (EFI_LBA)lba, (UINTN)count, buf, g_RBIP ? g_RBIP : g_BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int flush_f(struct block_device* dev) {
    int status = g_BIP->FlushBlocks(g_RBIP ? g_RBIP : g_BIP);
    if (EFI_ERROR(status)) return 0;
    return 1;
}

EFIAPI void wait_key(CHAR16* msg) {
    g_ST->ConOut->OutputString(g_ST->ConOut, msg);
    g_ST->ConOut->OutputString(g_ST->ConOut, u"\n[Press any key to continue]\n");
    
    EFI_INPUT_KEY key;
    // Clear the buffer first
    g_ST->ConIn->Reset(g_ST->ConIn, FALSE);
    // Wait for a stroke
    while (g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &key) == EFI_NOT_READY);
}

EFIAPI static bool draw_menu_frame(void) {
    if (!g_ST) return false;

    UINTN start_x = 0;
    UINTN start_y = 0;
    UINTN width = 0;
    UINTN height = 0;

    int status = g_ST->ConOut->QueryMode(g_ST->ConOut, g_ST->ConOut->Mode->Mode, &width, &height);
    if (EFI_ERROR(status)) return false;

    // Draw Top
    status = g_ST->ConOut->ClearScreen(g_ST->ConOut);
    if (EFI_ERROR(status)) return false;
    status = g_ST->ConOut->SetCursorPosition(g_ST->ConOut, start_x, start_y);
    if (EFI_ERROR(status)) return false;

    status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2554");
    if (EFI_ERROR(status)) return false;
    for (UINTN i = 0; i < width - 2; i++) {
        status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2550");
        if (EFI_ERROR(status)) return false;
    }
    status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2557");
    if (EFI_ERROR(status)) return false;

    // Draw Sides
    for(UINTN i = 1; i < height - 1; i++) {
        status = g_ST->ConOut->SetCursorPosition(g_ST->ConOut, start_x, start_y + i) ;
        if (EFI_ERROR(status)) return false;
        status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2551");
        if (EFI_ERROR(status)) return false;
        status = g_ST->ConOut->SetCursorPosition(g_ST->ConOut, start_x + width - 1, start_y + i) ;
        if (EFI_ERROR(status)) return false;
        status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2551");
        if (EFI_ERROR(status)) return false;
    }

    // Draw Bottom
    status = g_ST->ConOut->SetCursorPosition(g_ST->ConOut, start_x, start_y + height - 1) ;
    if (EFI_ERROR(status)) return false;
    status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u255A");
    if (EFI_ERROR(status)) return false;
    for (UINTN i = 0; i < width - 2; i++) {
        status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u2550");
        if (EFI_ERROR(status)) return false;
    }
    status = g_ST->ConOut->OutputString(g_ST->ConOut, u"\u255D");
    if (EFI_ERROR(status)) return false;

    return true;
}

EFIAPI static EFI_BLOCK_IO_PROTOCOL* get_block_io(EFI_HANDLE ImageHandle) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
    EFI_BLOCK_IO_PROTOCOL* block_io;

    wait_key(u"Doing Handle Protocol");
    status = g_BS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&loaded_image);
    if (EFI_ERROR(status))
        return NULL;

    wait_key(u"Doing Handle Protocol again");
    status = g_BS->HandleProtocol(loaded_image->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID**)&block_io);
    if (EFI_ERROR(status))
        return NULL;

    wait_key(u"Done?");
    return block_io;
}

EFIAPI static void char_to_char16(char* s, CHAR16* out, size_t out_size) {
    size_t i = 0;

    for (; s[i] && i < out_size - 1; i++)
        out[i] = (CHAR16)s[i];

    out[i] = 0;
}

EFIAPI static UINT16 read_input(void) {
    EFI_INPUT_KEY key;
    EFI_STATUS status;

    while (1) {
        status = g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &key);
        if (!EFI_ERROR(status)) {
            return key.UnicodeChar ? key.UnicodeChar : key.ScanCode;
        }
    }
}

EFIAPI static EFI_BLOCK_IO_PROTOCOL* get_physical_disk_io(void) {
    UINTN handle_count = 0;
    EFI_HANDLE* handles = NULL;
    EFI_STATUS status = g_BS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &handle_count, &handles);
    if (EFI_ERROR(status)) return NULL;

    for (UINTN i = 0; i < handle_count; i++) {
        EFI_BLOCK_IO_PROTOCOL* bio;
        if (EFI_ERROR(g_BS->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (void**)&bio)))
            continue;

        if (bio && bio->Media && bio->Media->BlockSize > 0 && !bio->Media->LogicalPartition) {
            return bio;
        }
    }
    return NULL;
}

EFIAPI EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    if (SystemTable == NULL) return EFI_LOAD_ERROR;

    g_ST = SystemTable;
    g_BS = SystemTable->BootServices;
    g_IH = ImageHandle;

    int status = SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    if (EFI_ERROR(status)) return status;

    wait_key(u"Starting block IO");
    g_BIP = get_block_io(ImageHandle);
    g_RBIP = get_physical_disk_io();
    if (g_BIP == NULL) return EFI_LOAD_ERROR;

    status = SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
    if (EFI_ERROR(status)) return status;

    status = SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
    if (EFI_ERROR(status)) return status;
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Welcome To AOS Bootloader!\n");
    if (EFI_ERROR(status)) return status;
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Updating System Information...\n");
    if (EFI_ERROR(status)) return status;

    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    SystemInfo->boot_drive = 0x0;
    SystemInfo->boot_mode = 0;
    SystemInfo->reserved0 = 0;
    SystemInfo->cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo->cpu_vendor);
    SystemInfo->apic_present = check_apic();
    SystemInfo->tsc_freq_hz = measure_tsc();
    SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(*SystemInfo) - 1);

    struct pbfs_funcs funcs = {
        .malloc=btl_malloc,
        .free=btl_free
    };

    struct pbfs_mount mnt = {0};
    struct block_device dev = {0};

    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Initializing PBFS...\n");
    if (EFI_ERROR(status)) return status;
    dev.read_block = read_blk;
    dev.write_block = write_blk;
    dev.read = read_f;
    dev.write = write_f;
    dev.flush = flush_f;
    dev.block_size = !g_RBIP ? (uint32_t)g_BIP->Media->BlockSize : (uint32_t)g_RBIP->Media->BlockSize;
    dev.block_count = !g_RBIP ? (uint64_t)(g_BIP->Media->LastBlock + 1) : (uint32_t)(g_RBIP->Media->LastBlock + 1);
    dev.name = "UEFI DISK";
    pbfs_init(&funcs);
    
    int out = pbfs_mount(&dev, &mnt);
    if (out != PBFS_RES_SUCCESS) {
        status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Failed to mount, Error:\n\t");
        if (EFI_ERROR(status)) return status;
        CHAR16 err[128];
        char_to_char16(pbfs_get_err_str(out), err, 128);
        status = SystemTable->ConOut->OutputString(SystemTable->ConOut, err);
        if (EFI_ERROR(status)) return status;
        return EFI_LOAD_ERROR;
    }

    PBFS_Kernel_Entry os_entries[30];
    uint64_t entry_count = 0;
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Full initialization complete!\n");
    if (EFI_ERROR(status)) return status;

    if (!draw_menu_frame()) return EFI_LOAD_ERROR;

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 17, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    UINTN width = 0;
    UINTN height = 0;

    status = g_ST->ConOut->QueryMode(g_ST->ConOut, g_ST->ConOut->Mode->Mode, &width, &height);
    if (EFI_ERROR(status)) return status;

    int selected = 0;
    int back_to_uefi_settings_idx = 0;
    int running = 1;
    CHAR16* help_text = u"Use UP/DOWN to select, ENTER to boot";
    size_t help_text_len = 36;
    size_t help_text_x = (width - help_text_len) / 2;
    uint64_t entry_count_fit = entry_count > (height - 4) ? (height - 4) : entry_count;

    CHAR16* empty = btl_malloc((width - 1) * sizeof(CHAR16));
    for(UINTN i = 0; i < width-2; i++)
        empty[i] = L' ';
    empty[width-2] = 0;

    while (running) {
        for (int i = 0; i < entry_count_fit; i++) {
            status = SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 1, 4 + i);
            if (EFI_ERROR(status)) return status;
            if(i == selected) {
                status = SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x00 | (0x0F << 4));
                if (EFI_ERROR(status)) return status;
            } else {
                status = SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
                if (EFI_ERROR(status)) return status;
            }

            if(i < entry_count_fit - 1) {
                CHAR16 nm[128];
                char_to_char16(os_entries[i].name, nm, 128);
                status = SystemTable->ConOut->OutputString(SystemTable->ConOut, nm);
                if (EFI_ERROR(status)) return status;
            } else if (i == entry_count_fit - 1) {
                back_to_uefi_settings_idx = i;
                status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"UEFI Firmware Settings");
                if (EFI_ERROR(status)) return status;
            } else {
                status = SystemTable->ConOut->OutputString(SystemTable->ConOut, empty);
                if (EFI_ERROR(status)) return status;
            }
        }

        status = SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
        if (EFI_ERROR(status)) return status;
        status = SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, help_text_x, 1);
        if (EFI_ERROR(status)) return status;
        status = SystemTable->ConOut->OutputString(SystemTable->ConOut, help_text);
        if (EFI_ERROR(status)) return status;

        UINT16 sc = read_input();
        if (sc == 0x0001 && selected > 0) {
            selected--;
        } else if (sc == 0x0002 && selected < entry_count - 1) {
            selected++;
        } else if (sc == '\r' || sc == 0x0000) {
            running = 0;
        }
    }

    if (selected == back_to_uefi_settings_idx) {
        g_BS->Exit(g_IH, EFI_SUCCESS, 0, NULL);
    }

    status = SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    if (EFI_ERROR(status)) return status;
    status = SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
    if (EFI_ERROR(status)) return status;
    status = SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
    if (EFI_ERROR(status)) return status;
    
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Loading ");
    if (EFI_ERROR(status)) return status;
    CHAR16 nm[128];
    char_to_char16(os_entries[selected].name, nm, 128);
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, nm);
    if (EFI_ERROR(status)) return status;
    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"...\n");
    if (EFI_ERROR(status)) return status;

    UINTN pages = ((uint128_to_u64(os_entries[selected].count) * dev.block_size) + 0x1000 - 1) / 0x1000;
    status = g_BS->AllocatePages(AllocateAddress, EfiLoaderCode, pages, (EFI_PHYSICAL_ADDRESS*)AOS_KERNEL_LOC);
    if (EFI_ERROR(status)) return status;

    if (read_f(&dev, uint128_to_u64(os_entries[selected].lba), uint128_to_u64(os_entries[selected].count), AOS_KERNEL_LOC) != 1) {
        status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Failed to read kernel!\n");
        if (EFI_ERROR(status)) return status;
        return EFI_LOAD_ERROR;
    }

    status = SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Jumping to kernel...\n");
    if (EFI_ERROR(status)) return status;

    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;

    status = g_BS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    map_size += desc_size * 2;
    status = g_BS->AllocatePool(EfiLoaderData, map_size, (void**)&map);
    if (EFI_ERROR(status)) return status;
    status = g_BS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status)) return status;
    status = g_BS->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(status)) return status;

    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    return EFI_LOAD_ERROR;
}