#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/core/acpi.h>
#include <inc/drivers/io/drive.h>

#include <stddef.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs.h>
#include <PBFS/headers/pbfs_structs.h>
#include <PBFS/headers/pbfs_structs_64.h>
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

void pm_print_hex(struct VMemDesign* cursor, unsigned int val) {
    char hex[9];
    const char *digits = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        hex[i] = digits[val & 0xF];
        val >>= 4;
    }
    hex[8] = '\0';
    vmem_print(cursor, hex);
}

void cpuid_get_vendor(char *vendor_out) {
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

uint32_t cpuid_signature(void) {
    uint32_t eax;
    __asm__ volatile ("cpuid"
        : "=a"(eax)
        : "a"(1)
        : "ebx", "ecx", "edx");
    return eax;
}

void enable_a20(void) {
    asm volatile(
        "inb $0x92, %%al\n\t"
        "orb $0x02, %%al\n\t"
        "outb %%al, $0x92\n\t"
        :
        :
        : "al"
    );
}

__attribute__((naked, noreturn))
void stage3_jump_to_kernel(void (*kernel)(void), uint64_t stack_top) {
    __asm__ __volatile__ (
        "mov %rsi, %rsp\n\t" // stack_top is 2nd arg (rsi)
        "mov %rsi, %rbp\n\t"
        "cli\n\t"
        "cld\n\t"
        "jmp *%rdi\n\t" // kernel pointer is 1st arg (rdi)
    );
}

uint8_t check_apic(void) { return 0; } // Stub for now
uint64_t measure_tsc(void) { return 0; } // Stub for now

uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

static uint64_t free_ptr = 0x08200000; // 130MB
static drive_device_t cur_drive = {0};
static uint8_t found_drive = 0;

void* btl_malloc(size_t size) {
    uint64_t ret = free_ptr;
    free_ptr += size;
    return (void*)ret;
}
void btl_free(void* ptr) {
    return; // Useless
}

void draw_menu_frame(struct VMemDesign* cursor) {
    int width = IO_VMEM_MAX_COLS;
    int height = IO_VMEM_MAX_ROWS;
    int start_x = 0;
    int start_y = 0;

    // Draw Top
    vmem_set_cursor(start_x, start_y);
    cursor->x = start_x;
    cursor->y = start_y;
    vmem_printc(cursor, 0xC9); // ╔
    for(int i = 0; i < width - 2; i++) vmem_printc(cursor, 0xCD); // ═
    vmem_printc(cursor, 0xBB); // ╗

    // Draw Sides
    for(int i = 1; i < height - 1; i++) {
        vmem_set_cursor(start_x, start_y + i);
        cursor->x = start_x;
        cursor->y = start_y + i;
        vmem_printc(cursor, 0xBA); // ║
        vmem_set_cursor(start_x + width - 1, start_y + i);
        cursor->x = start_x + width - 1;
        cursor->y = start_y + i;
        vmem_printc(cursor, 0xBA); // ║
    }

    // Draw Bottom
    vmem_set_cursor(start_x, start_y + height - 1);
    cursor->x = start_x;
    cursor->y = start_y + height - 1;
    vmem_printc(cursor, 0xC8); // ╚
    for(int i = 0; i < width - 2; i++) vmem_printc(cursor, 0xCD); // ═
    vmem_printc(cursor, 0xBC); // ╝
}

int read_blk(struct block_device* dev, uint64_t lba, void* buf) {
    if (found_drive == 1) {
        cur_drive.read_blk(cur_drive.cur_port, lba, 1, buf);
    }
    return 1;
}
int write_blk(struct block_device* dev, uint64_t lba, const void* buf) {
    if (found_drive == 1) {
        cur_drive.write_blk(cur_drive.cur_port, lba, 1, buf);
    }
    return 1;
}
int read_f(struct block_device* dev, uint64_t lba, uint64_t count, void* buf) {
    if (found_drive == 1) {
        cur_drive.read_blk(cur_drive.cur_port, lba, count, buf);
    }
    return 1;
}
int write_f(struct block_device* dev, uint64_t lba, uint64_t count, const void* buf) {
    if (found_drive == 1) {
        cur_drive.write_blk(cur_drive.cur_port, lba, count, buf);
    }
    return 1;
}
int flush_f(struct block_device* dev) {
    if (found_drive == 1) {
        cur_drive.flush(cur_drive.cur_port);
    }
    return 1;
}

void stage3(void) __attribute__((section(".entry"), used)); 
void stage3(void) {
    enable_a20();
    struct VMemDesign cursor = {
        .x = 0,
        .y = 0,
        .fg = VMEM_COLOR_WHITE,
        .bg = VMEM_COLOR_BLACK,
        .serial_out = 1
    };

    acpi_init();

    vmem_set_cursor(0, 0);
    vmem_clear_screen(&cursor);
    vmem_print(&cursor, "Welcome To AOS Bootloader! Debugging...\n");
    
    unsigned char *mem = (unsigned char *)0x100000;
    *mem = 0xAA;
    if (*mem != 0xAA) {
        cursor.fg = VMEM_COLOR_RED;
        vmem_print(&cursor, "A20 line disabled!\n");
        for (;;) asm("hlt");
    }

    uint8_t boot_drive = *AOS_BOOT_INFO_LOC; // Get Boot drive
    vmem_print(&cursor, "Updating System info...\n");
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    SystemInfo->boot_drive = boot_drive;
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

    if (get_available_drives(&cur_drive) == 1) {
        if (cur_drive.active != 1) {
            if (cur_drive.init != NULL) cur_drive.init();
        }
        if (cur_drive.read_blk == NULL) {
            vmem_print(&cursor, "No read function for drive?\n");
        } else {
            found_drive = 1;
        }
    }

    if (found_drive != 1) {
        vmem_print(&cursor, "No supported drive found!\n");
        for (;;) asm volatile("hlt");
    } else {
        vmem_print(&cursor, "Initializing PBFS...\n");
        cur_drive.block_dev.read_block = read_blk;
        cur_drive.block_dev.write_block = write_blk;
        cur_drive.block_dev.read = read_f;
        cur_drive.block_dev.write = write_f;
        cur_drive.block_dev.flush = flush_f;
        pbfs_init(&funcs);
        if(pbfs_mount(&cur_drive.block_dev, &mnt) != PBFS_RES_SUCCESS) {
            vmem_print(&cursor, "Failed to mount!\n");
            for (;;) asm volatile("hlt");
        }
    }

    PBFS_Kernel_Entry os_entries[17];
    uint64_t entry_count = 0;
    vmem_print(&cursor, "Full initialization complete!\n");
    
    cursor.serial_out = 0;
    vmem_clear_screen(&cursor);
    draw_menu_frame(&cursor);

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 17, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    int selected = 0;
    int running = 1;
    const char* help_text = "Use UP/DOWN to select, ENTER to boot.";
    size_t help_text_len = strlen(help_text);
    size_t help_text_x = (help_text_len > IO_VMEM_MAX_COLS ? IO_VMEM_MAX_COLS : help_text_len) / 2;
    uint64_t entry_count_fit = entry_count > (IO_VMEM_MAX_COLS - 4) ? (IO_VMEM_MAX_COLS - 4) : entry_count;

    while (running) {
        for (int i = 0; i < entry_count_fit; i++) {
            vmem_set_cursor(1, 4 + i);
            cursor.x = 1;
            cursor.y = 4 + i;
            if(i == selected) {
                cursor.fg = VMEM_COLOR_BLACK;
                cursor.bg = VMEM_COLOR_WHITE; // Highlight bar
            } else {
                cursor.fg = VMEM_COLOR_WHITE;
                cursor.bg = VMEM_COLOR_BLACK;
            }

            if(i < entry_count_fit) {
                vmem_print(&cursor, os_entries[i].name);
            } else {
                vmem_print(&cursor, "                "); // Clear empty slots
            }
        }

        cursor.fg = VMEM_COLOR_CYAN;
        cursor.bg = VMEM_COLOR_BLACK;
        vmem_set_cursor(help_text_x, 1);
        cursor.x = help_text_x;
        cursor.y = 1;
        vmem_print(&cursor, help_text);

        uint8_t scancode = ps2_read_scan(); 
        if(scancode == 0x48) { // Up Arrow
            if(selected > 0) selected--;
        } else if(scancode == 0x50) { // Down Arrow
            if(selected < entry_count_fit - 1) selected++;
        } else if(scancode == 0x1C) { // Enter
            running = 0;
        }
    }

    vmem_clear_screen(&cursor);
    vmem_set_cursor(0, 0);
    cursor.bg = VMEM_COLOR_BLACK;
    cursor.fg = VMEM_COLOR_WHITE;
    cursor.serial_out = 1;
    vmem_printf(&cursor, "Loading %s...\n", os_entries[selected].name);

    if (!cur_drive.read_blk(cur_drive.cur_port, uint128_to_u64(os_entries[selected].lba), uint128_to_u32(os_entries[selected].count), AOS_KERNEL_LOC)) {
        vmem_print(&cursor, "Failed to read kernel, Disk error!\n");
        for (;;) asm volatile("hlt");
    }

    vmem_print(&cursor, "Jumping to Kernel...\n");
    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

asm(".globl stage3");
