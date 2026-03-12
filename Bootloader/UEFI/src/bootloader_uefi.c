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

#include <system.h>

EFIAPI static void cpuid_get_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    __asm__ volatile (
        "pushq %%rbx\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "popq %%rbx"
        : "=a"(eax), "=r"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
        : "cc"
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
    int status = pefi_read_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, 1, buf, (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data > 0 ? (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data : (EFI_BLOCK_IO_PROTOCOL*)(dev->driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*)));
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_blk(struct block_device* dev, uint64_t lba, const void* buf) {
    if (pefi_state.initialized != 1) return 0;
    int status = pefi_write_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, 1, buf, (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data > 0 ? (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data : (EFI_BLOCK_IO_PROTOCOL*)(dev->driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*)));
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int read_f(struct block_device* dev, uint64_t lba, uint64_t count, void* buf) {
    if (pefi_state.initialized != 1) return 0;
    int status = pefi_read_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, (UINTN)count, buf, (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data > 0 ? (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data : (EFI_BLOCK_IO_PROTOCOL*)(dev->driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*)));
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int write_f(struct block_device* dev, uint64_t lba, uint64_t count, const void* buf) {
    if (pefi_state.initialized != 1) return 0;
    int status = pefi_write_lba(pefi_state.system_table, pefi_state.image_handle, (EFI_LBA)lba, (UINTN)count, buf, (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data > 0 ? (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data : (EFI_BLOCK_IO_PROTOCOL*)(dev->driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*)));
    if (EFI_ERROR(status)) return 0;
    return 1;
}
EFIAPI static int flush_f(struct block_device* dev) {
    if (pefi_state.initialized != 1) return 0;
    EFI_BLOCK_IO_PROTOCOL* BIP = (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data > 0 ? (EFI_BLOCK_IO_PROTOCOL*)dev->driver_data : (EFI_BLOCK_IO_PROTOCOL*)(dev->driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*));
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

    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2554");
    for (UINTN i = 0; i < width - 2; i++) {
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2550");
    }
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2557");

    // Draw Sides
    for(UINTN i = 1; i < height - 1; i++) {
        pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x, start_y + i);
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2551");
        pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x + width - 1, start_y + i);
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2551");
    }

    // Draw Bottom
    pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, start_x, start_y + height - 1);
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u255A");
    for (UINTN i = 0; i < width - 2; i++) {
        pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u2550");
    }
    pefi_state.system_table->ConOut->OutputString(pefi_state.system_table->ConOut, u"\u255D");

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

EFIAPI static EFI_BLOCK_IO_PROTOCOL* get_physical_disk_io(void) {
    if (pefi_state.initialized != 1) return NULL;

    UINTN handle_count = 0;
    EFI_HANDLE* handles = NULL;
    EFI_STATUS status = pefi_state.boot_services->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &handle_count, &handles);
    if (EFI_ERROR(status)) return NULL;

    for (UINTN i = 0; i < handle_count; i++) {
        EFI_BLOCK_IO_PROTOCOL* bio;
        if (EFI_ERROR(pefi_state.boot_services->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (void**)&bio)))
            continue;

        if (bio && bio->Media && bio->Media->BlockSize > 0 && !bio->Media->LogicalPartition) {
            return bio;
        }
    }
    return NULL;
}

EFIAPI EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    __asm__ volatile ("andq $-16, %rsp");
    if (SystemTable == NULL) return EFI_LOAD_ERROR;

    InitalizeLib(SystemTable, ImageHandle);
    if (pefi_state.initialized != 1) {
        pefi_print(SystemTable, u"Failed to initalize PEFI!\n");
        for (;;) asm volatile("hlt");
    }

    pefi_clear(SystemTable);

    pefi_print(SystemTable, u"Welcome to AOS Bootloader!\n");
    pefi_print(SystemTable, u"Getting Block IOs...\n");
    EFI_BLOCK_IO_PROTOCOL* g_BIP = NULL;
    EFI_BLOCK_IO_PROTOCOL* g_RBIP = NULL;

    g_BIP = pefi_find_block_io(SystemTable);
    if (g_BIP == NULL) {
        pefi_print(SystemTable, u"Failed to find Block IO!\n");
        for (;;) asm volatile("hlt");
    }
    g_RBIP = get_physical_disk_io();
    pefi_print(SystemTable, u"Updating SysInfo...\n");
    
    EFI_PHYSICAL_ADDRESS sysinfo_addr = AOS_SYS_INFO_ADDR;
    if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAddress, EfiLoaderData, 1, &sysinfo_addr))) {
        pefi_print(SystemTable, u"Failed to allocate memory for SystemInfo structure!\n");
        for (;;) asm volatile("hlt");
    }
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)(UINTN)sysinfo_addr;;
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

    pefi_print(SystemTable, u"Initializing PBFS...\n");
    dev.read_block = read_blk;
    dev.write_block = write_blk;
    dev.read = read_f;
    dev.write = write_f;
    dev.flush = flush_f;
    dev.block_size = !g_RBIP ? (uint32_t)g_BIP->Media->BlockSize : (uint32_t)g_RBIP->Media->BlockSize;
    dev.block_count = !g_RBIP ? (uint64_t)(g_BIP->Media->LastBlock + 1) : (uint32_t)(g_RBIP->Media->LastBlock + 1);
    dev.name = "UEFI DISK";
    
    dev.driver_data = btl_malloc(sizeof(EFI_BLOCK_IO_PROTOCOL*) * 2);
    if (!dev.driver_data) {
        pefi_print(SystemTable, u"Failed to allocate memory!\n");
        for (;;) asm volatile("hlt");
    }
    memcpy(dev.driver_data, &g_RBIP, sizeof(EFI_BLOCK_IO_PROTOCOL*));
    memcpy(dev.driver_data + sizeof(EFI_BLOCK_IO_PROTOCOL*), &g_BIP, sizeof(EFI_BLOCK_IO_PROTOCOL*));

    pbfs_init(&funcs);
    
    int out = pbfs_mount(&dev, &mnt);
    if (out != PBFS_RES_SUCCESS) {
        pefi_print(SystemTable, u"Failed to mount, Error:\n\t");
        CHAR16 err[128];
        char_to_char16(pbfs_get_err_str(out), err, 128);
        pefi_print(SystemTable, err);
        for (;;) asm volatile("hlt");
    }

    PBFS_Kernel_Entry os_entries[30];
    uint64_t entry_count = 0;
    pefi_print(SystemTable, u"Full initialization complete!\n");

    if (!draw_menu_frame()) {
        pefi_print(SystemTable, u"Failed to draw menu frame!\n");
        for (;;) asm volatile("hlt");
    }

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 17, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    UINTN width = 0;
    UINTN height = 0;

    pefi_state.system_table->ConOut->QueryMode(pefi_state.system_table->ConOut, pefi_state.system_table->ConOut->Mode->Mode, &width, &height);

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
            SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 1, 4 + i);
            if(i == selected) {
                SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x00 | (0x0F << 4));
            } else {
                SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
            }

            if(i < entry_count_fit - 1) {
                CHAR16 nm[128];
                char_to_char16(os_entries[i].name, nm, 128);
                pefi_print(SystemTable, nm);
            } else if (i == entry_count_fit - 1) {
                back_to_uefi_settings_idx = i;
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
        } else if (sc == 0x0002 && selected < entry_count - 1) {
            selected++;
        } else if (sc == '\r' || sc == 0x0000) {
            running = 0;
        }
    }

    if (selected == back_to_uefi_settings_idx) {
        pefi_state.boot_services->Exit(ImageHandle, EFI_SUCCESS, 0, NULL);
    }

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
    SystemTable->ConOut->SetAttribute(SystemTable->ConOut, 0x0F | (0x00 << 4));
    
    pefi_print(SystemTable, u"Loading ");
    CHAR16 nm[128];
    char_to_char16(os_entries[selected].name, nm, 128);
    pefi_print(SystemTable, nm);
    pefi_print(SystemTable, u"...\n");

    UINTN pages = ((uint128_to_u64(os_entries[selected].count) * dev.block_size) + 0x1000 - 1) / 0x1000;
    pefi_state.boot_services->AllocatePages(AllocateAddress, EfiLoaderCode, pages, (EFI_PHYSICAL_ADDRESS*)AOS_KERNEL_LOC);

    if (read_f(&dev, uint128_to_u64(os_entries[selected].lba), uint128_to_u64(os_entries[selected].count), AOS_KERNEL_LOC) != 1) {
        pefi_print(SystemTable, u"Failed to read kernel!\n");
        for (;;) asm volatile("hlt");
    }

    pefi_print(SystemTable, u"Jumping to kernel...\n");
    btl_free(dev.driver_data);
    btl_free(empty);

    UINTN map_size = 0;
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;

    int status = pefi_state.boot_services->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    map_size += desc_size * 2;
    pefi_state.boot_services->AllocatePool(EfiLoaderData, map_size, (void**)&map);
    pefi_state.boot_services->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);
    pefi_state.boot_services->ExitBootServices(ImageHandle, map_key);

    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    pefi_print(SystemTable, u"Failed to start Kernel!\n");
    for (;;) asm volatile("hlt");
}