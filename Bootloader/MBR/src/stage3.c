#include <system.h>
#include <asm.h>

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

#include <ambrc.h>
#include <panic_shell.h>

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

#define CMOS_BYTE_IDX_AOSB_FLAGS 0x1A
#define CMOS_BYTE_IDX_KERNEL_INFO 0x1B

#define CMOS_AOSB_FLAG_SAFE_MODE (1 << 0)
#define CMOS_AOSB_FLAG_PANIC_MODE (1 << 1)

#define CMOS_KERNEL_INFO_KERNEL_ACTIVE (1 << 0)

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

void write_cmos(uint8_t index, uint8_t value) {
    asm_outb(0x70, index); // select CMOS byte
    asm_outb(0x71, value); // write value
}

uint8_t read_cmos(uint8_t index) {
    asm_outb(0x70, index); // select CMOS byte
    return asm_inb(0x71); // read value
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

uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

static uint64_t free_ptr = 0x08200000; // 130MB
static drive_device_t cur_drive = {0};
static uint8_t found_drive = 0;

static uint8_t current_mode = 0; // 1 = Safe, 2 = Panic, 0 = Normal
static uint8_t rdtscp_supported = 0;

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
    int start_y = 1; // After id

    // Draw id
    const char* id = "AOS Bootloader V1.0";
    int id_x = (IO_VMEM_MAX_COLS - strlen(id)) / 2;
    cursor->x = id_x;
    cursor->y = 0;
    vmem_print(cursor, id);

    // Draw Top
    cursor->x = start_x;
    cursor->y = start_y;
    vmem_printc(cursor, 0xC9); // ╔
    for(int i = 0; i < width - 2; i++) vmem_printc(cursor, 0xCD); // ═
    vmem_printc(cursor, 0xBB); // ╗

    // Draw Sides
    for(int i = 1; i < height - 2; i++) {
        cursor->x = start_x;
        cursor->y = start_y + i;
        vmem_printc(cursor, 0xBA); // ║
        cursor->x = start_x + width - 1;
        cursor->y = start_y + i;
        vmem_printc(cursor, 0xBA); // ║
    }

    // Draw Bottom
    cursor->x = start_x;
    cursor->y = start_y + height - 2;
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

void display_splash(struct ambrc* ambrc, struct VMemDesign* cursor) {
    size_t x = (IO_VMEM_MAX_COLS/2)-16;
    cursor->x = x;
    cursor->y = (IO_VMEM_MAX_ROWS/2)-4;
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

    kdelay((ambrc->display.splash_duration * 1000) > 0xFFFFFFFF ? 0xFFFFFFFF : ambrc->display.splash_duration * 1000);
}

void display_popup(struct ambrc* ambrc, struct VMemDesign* design, const char* popup_msg) {
    size_t pmsg_len = ALIGN_UP(strlen(popup_msg), 2);
    uint32_t box_w = pmsg_len + 2 > IO_VMEM_MAX_COLS ? IO_VMEM_MAX_COLS : pmsg_len + 2;
    uint32_t box_h = 12;
    uint32_t start_x = (IO_VMEM_MAX_COLS / 2) - (box_w / 2);
    uint32_t start_y = (IO_VMEM_MAX_ROWS / 2) - (box_h / 2);

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
                    if (++box_h > IO_VMEM_MAX_ROWS) return;
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

    // Draw top
    vmem_printc(design, 0xC9);
    for(int i = 0; i < box_w - 2; i++) vmem_printc(design, 0xCD);
    vmem_printc(design, 0xBB);

    // Draw Sides
    for(int i = 1; i < box_h - 1; i++) {
        design->x = start_x;
        design->y = start_y + i;
        vmem_printc(design, 0xBA);

        for(int j = 0; j < box_w - 2; j++) vmem_printc(design, ' ');

        design->x = start_x + box_w - 1;
    design->y = start_y + i;
        vmem_printc(design, 0xBA);
    }

    // Draw Bottom
    design->x = start_x;
    design->y = start_y + box_h - 1;
    vmem_printc(design, 0xC8);
    for(int i = 0; i < box_w - 2; i++) vmem_printc(design, 0xCD);
    vmem_printc(design, 0xBC);

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

uint64_t read_tsc(void) {
    uint32_t low = 0;
    uint32_t high = 0;
    if (rdtscp_supported == 1) {
        asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
        asm volatile("cpuid" : : : "rax", "rcx", "rdx", "memory");
    }
    else {
        asm volatile("cpuid" : : : "rax", "rcx", "rdx", "memory");
        asm volatile("rdtsc" : "=a"(low), "=d"(high) : : "memory");
    }
    return ((uint64_t)high << 32) | low;
}

void stage3(void) __attribute__((section(".entry"), used)); 
void stage3(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000001;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    if (edx & (1 << 27)) {
        rdtscp_supported = 1;
    } else {
        rdtscp_supported = 0;
    }
    uint64_t tsc_start = read_tsc();

    enable_a20();
    init_backup_ambrc();
    struct ambrc* ambrc = get_ambrc();

    struct VMemDesign cursor = {
        .x = 0,
        .y = 0,
        .fg = ambrc->display.fg_color,
        .bg = ambrc->display.bg_color,
        .serial_out = 0
    };

    acpi_init();

    ktimer_calibrate();

    uint64_t tsc_start2 = read_tsc();
    kdelay(1);
    uint64_t tsc_end = read_tsc();
    uint64_t cycles_per_ms = tsc_end - tsc_start2;

    switch (ambrc->boot_info.crash_verification_mode) {
        case 0: break;
        case 1: 
            current_mode = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 2 : 0;
            break;
        case 2:
            current_mode = read_cmos(CMOS_BYTE_IDX_KERNEL_INFO) & CMOS_KERNEL_INFO_KERNEL_ACTIVE ? 2 : 0;
            break;
        case 3:
            uint64_t tsc_is_fine = (uint64_t)(tsc_start / cycles_per_ms) > 64000 ? 0 : 1;
            uint64_t kernel_is_fine = read_cmos(CMOS_BYTE_IDX_KERNEL_INFO) & CMOS_KERNEL_INFO_KERNEL_ACTIVE ? 0 : 1;
            current_mode = !tsc_is_fine && !kernel_is_fine ? 2 : tsc_is_fine && kernel_is_fine ? 0 : 1;
            break;
        default: break;
    }

    switch (current_mode) {
        case 1: write_cmos(CMOS_BYTE_IDX_AOSB_FLAGS, CMOS_AOSB_FLAG_SAFE_MODE); break;
        case 2: write_cmos(CMOS_BYTE_IDX_AOSB_FLAGS, CMOS_AOSB_FLAG_PANIC_MODE); break;
        default: break;
    }

    vmem_disable_cursor();

    vmem_clear_screen(&cursor);
    if (ambrc->display.splash_duration > 0) display_splash(ambrc, &cursor);
    
    unsigned char *mem = (unsigned char *)0x100000;
    *mem = 0xAA;
    if (*mem != 0xAA) {
        cursor.fg = ambrc->display.error_fg_color;
        vmem_print(&cursor, "A20 line disabled!\n");
        for (;;) asm("hlt");
    }

    uint8_t boot_drive = *AOS_BOOT_INFO_LOC; // Get Boot drive
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    SystemInfo->boot_drive = boot_drive;
    SystemInfo->boot_mode = 0;
    SystemInfo->reserved0 = 0;
    SystemInfo->cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo->cpu_vendor);
    SystemInfo->checksum = 0;
    SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(aos_sysinfo_t));

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
        int out = pbfs_mount(&cur_drive.block_dev, &mnt);
        if (out != PBFS_RES_SUCCESS) {
            vmem_printf(&cursor, "Failed to mount, Error:\n\t%s\n", pbfs_get_err_str(out));
            for (;;) asm volatile("hlt");
        }
    }

    PBFS_Kernel_Entry os_entries[20];
    uint64_t entry_count = 0;
    
    cursor.serial_out = 0;
    vmem_clear_screen(&cursor);
    draw_menu_frame(&cursor);

    if (pbfs_list_kernels(&mnt, (PBFS_Kernel_Entry*)os_entries, 20, &entry_count) != PBFS_RES_SUCCESS) {
        entry_count = 0;
    }

    int selected = 0;
    int old_selected = -1;
    int scroll_offset = 0;
    int running = 1;
    int ambrc_entry = ambrc->display.show_settings_at_top ? 0 : entry_count;
    int timeout = 0;
    uint64_t total_items = entry_count + 1;

    const int VIEWPORT_HEIGHT = (IO_VMEM_MAX_ROWS - 6);
    const int MENU_START_Y = 4;

    uint8_t force_redraw = 1;
    uint8_t popup_finished = 0;

    uint64_t timeout_start = read_tsc();

    while (running) {
        if ((ambrc->boot_info.default_os_idx > 0 || ambrc->boot_info.panic_os_idx > 0 || ambrc->boot_info.safe_os_idx > 0) && ambrc->boot_info.timeout != 0) {
            uint64_t timeout_cur = read_tsc();
            uint64_t timeout_elapsed = timeout_cur - timeout_start;
            if ((uint64_t)(timeout_elapsed / cycles_per_ms) > ambrc->boot_info.timeout * 1000) {
                timeout = 1;
                running = 0;
                continue;
            }
        }
        if (current_mode == 1 && !popup_finished) {
            display_popup(ambrc, &cursor, "Did anything crash? (y/n/C)");
            uint8_t popup_active = 1;
            while (popup_active) {
                uint8_t scancode = ps2_read_scan(); 
                switch (scancode) {
                    case 0x15: // Y key
                        current_mode = 2;
                        write_cmos(CMOS_BYTE_IDX_AOSB_FLAGS, CMOS_AOSB_FLAG_PANIC_MODE);
                        start_panic_shell(&cur_drive);
                        popup_active = 0;
                        popup_finished = 1;
                        break;
                    case 0x31: // N key
                        current_mode = 0;
                        write_cmos(CMOS_BYTE_IDX_AOSB_FLAGS, 0);
                        popup_active = 0;
                        popup_finished = 1;
                        break;
                    case 0x2E: // C Key
                    case 0x01: // ESC Key
                        popup_active = 0;
                        popup_finished = 1;
                        break;
                }
            }
            vmem_clear_screen(&cursor);
            draw_menu_frame(&cursor);
            timeout_start = read_tsc(); // Reset
            force_redraw = 1; 
        }
        if (force_redraw) {
            for (int i = 0; i < VIEWPORT_HEIGHT; i++) {
                int item_idx = scroll_offset + i;
                cursor.x = 1;
                cursor.y = MENU_START_Y + i;
                
                if (item_idx < total_items) {
                    uint8_t is_sel = (item_idx == selected);
                    cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
                    cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

                    const char* name;
                    if (item_idx == ambrc_entry) {
                        name = "AOS Bootloader Settings";
                    } else {
                        int k_idx = (ambrc_entry == 0) ? (item_idx - 1) : item_idx;
                        name = os_entries[k_idx].name;
                    }
                    
                    vmem_print(&cursor, name);
                    int len = strlen(name);
                    for (int j = 0; j < (IO_VMEM_MAX_COLS - 2 - len); j++) vmem_printc(&cursor, ' ');
                } else {
                    cursor.bg = ambrc->display.bg_color;
                    for (int j = 0; j < (IO_VMEM_MAX_COLS - 2); j++) vmem_printc(&cursor, ' ');
                }
            }
            force_redraw = 0;
            old_selected = selected;
        } 
        else if (selected != old_selected) {
            for (int i = 0; i < 2; i++) {
                int target = (i == 0) ? old_selected : selected;
                if (target < scroll_offset || target >= scroll_offset + VIEWPORT_HEIGHT) continue;

                cursor.x = 1;
                cursor.y = MENU_START_Y + (target - scroll_offset);
                uint8_t is_sel = (target == selected);
                cursor.fg = is_sel ? ambrc->display.selected_fg_color : ambrc->display.fg_color;
                cursor.bg = is_sel ? ambrc->display.selected_bg_color : ambrc->display.bg_color;

                const char* name;
                if (target == ambrc_entry) {
                    name = "AOS Bootloader Settings";
                } else {
                    int k_idx = (ambrc_entry == 0) ? (target - 1) : target;
                    name = os_entries[k_idx].name;
                }

                vmem_print(&cursor, name);
                int len = strlen(name);
                for (int j = 0; j < (IO_VMEM_MAX_COLS - 2 - len); j++) vmem_printc(&cursor, ' ');
            }
            old_selected = selected;
        }

        uint8_t scancode = ps2_try_read_scan();
        switch(scancode) {
            case 0x48: // Up
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset--;
                        force_redraw = 1;
                    }
                }
                break;
            case 0x50: // Down
                if (selected < total_items - 1) {
                    selected++;
                    if (selected >= scroll_offset + VIEWPORT_HEIGHT) {
                        scroll_offset++;
                        force_redraw = 1;
                    }
                }
                break;
            case 0x1C: // Enter
                if (selected == ambrc_entry) {
                    start_ambrc(&cur_drive);
                    ambrc = get_ambrc();
                    cursor.fg = ambrc->display.fg_color;
                    cursor.bg = ambrc->display.bg_color;
                    ambrc_entry = ambrc->display.show_settings_at_top ? 0 : (int)entry_count;
                    vmem_clear_screen(&cursor);
                    draw_menu_frame(&cursor);
                    force_redraw = 1;
                    timeout_start = read_tsc(); // Reset timeout
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

    if (!cur_drive.read_blk(cur_drive.cur_port, uint128_to_u64(os_entries[final_kernel_idx].lba), uint128_to_u32(os_entries[final_kernel_idx].count), (void*)ambrc->kernel_info[final_kernel_idx].load_addr)) {
        vmem_print(&cursor, "Failed to read kernel, Disk error!\n");
        for (;;) asm volatile("hlt");
    }

    vmem_print(&cursor, "Jumping to Kernel...\n");

    write_cmos(CMOS_BYTE_IDX_KERNEL_INFO, CMOS_KERNEL_INFO_KERNEL_ACTIVE);
    stage3_jump_to_kernel((void(*)(void))((void*)ambrc->kernel_info[final_kernel_idx].entry_point), AOS_KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

asm(".globl stage3");
