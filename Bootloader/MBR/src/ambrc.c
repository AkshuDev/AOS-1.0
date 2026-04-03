// AOS Master Boot Record Config
#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/io/drive.h>

#include <ambrc.h>

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

struct ambrc backup_ambrc = {
    .magic=AMBRC_MAGIC,
    .version=CURRENT_AMBRC_VERSION,
    .display=(struct ambrc_display){
        .bg_color = VMEM_COLOR_BLACK,
        .fg_color = VMEM_COLOR_WHITE,
        .selected_bg_color = VMEM_COLOR_WHITE,
        .selected_fg_color = VMEM_COLOR_BLACK,
        .ambrc_bg_color = VMEM_COLOR_BLACK,
        .ambrc_fg_color = VMEM_COLOR_WHITE,
        .ambrc_selected_bg_color = VMEM_COLOR_WHITE,
        .ambrc_selected_fg_color = VMEM_COLOR_BLACK,
        .splash_duration = 3
    },
    .boot_info=(struct ambrc_boot_info){
        .default_os_idx=0,
        .safe_os_idx=0,
        .panic_os_idx=0,
        .crash_verification_mode=3
    }
};

struct ambrc def_ambrc = {
    .magic=AMBRC_MAGIC,
    .version=CURRENT_AMBRC_VERSION,
    .display=(struct ambrc_display){
        .bg_color = VMEM_COLOR_BLACK,
        .fg_color = VMEM_COLOR_WHITE,
        .selected_bg_color = VMEM_COLOR_WHITE,
        .selected_fg_color = VMEM_COLOR_BLACK,
        .ambrc_bg_color = VMEM_COLOR_BLACK,
        .ambrc_fg_color = VMEM_COLOR_WHITE,
        .ambrc_selected_bg_color = VMEM_COLOR_WHITE,
        .ambrc_selected_fg_color = VMEM_COLOR_BLACK,
        .splash_duration = 3
    },
    .boot_info=(struct ambrc_boot_info){
        .default_os_idx=0,
        .safe_os_idx=0,
        .panic_os_idx=0,
        .crash_verification_mode=3
    }
};

static uint32_t crc32_table[256];
static int table_computed = 0;

static uint16_t active_k_idx = 0;
static uint8_t changes_made = 0;

static drive_device_t* gdrive = NULL;

void generate_crc32_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    table_computed = 1;
}

uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    if (!table_computed) generate_crc32_table();
    
    uint32_t crc = 0xFFFFFFFF; // Initial value
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF; // Final XOR
}

void init_backup_ambrc(void) {
    struct ambrc_kernel_info kinfo = {
        .load_addr=AOS_KERNEL_ADDR,
        .entry_point=AOS_KERNEL_ADDR,
        .safe_mode_flags=0
    };
    
    for (int i = 0; i < AMBRC_MAX_KERNELS; i++) memcpy(&backup_ambrc.kernel_info[i], &kinfo, sizeof(struct ambrc_kernel_info));

    backup_ambrc.crc32 = calculate_crc32((const uint8_t*)&backup_ambrc, sizeof(struct ambrc));

    for (int i = 0; i < AMBRC_MAX_KERNELS; i++) memcpy(&def_ambrc.kernel_info[i], &kinfo, sizeof(struct ambrc_kernel_info));

    def_ambrc.crc32 = calculate_crc32((const uint8_t*)&def_ambrc, sizeof(struct ambrc));
}

struct ambrc* get_ambrc(void) {
    struct ambrc* base_ambrc = (struct ambrc*)((uintptr_t)0x500);

    if (base_ambrc->magic != AMBRC_MAGIC) return &backup_ambrc;

    struct ambrc base_ambrc_copy = {0};
    memcpy(&base_ambrc_copy, base_ambrc, sizeof(struct ambrc));
    base_ambrc_copy.crc32 = 0;

    if (calculate_crc32((const uint8_t*)&base_ambrc_copy, sizeof(struct ambrc)) != base_ambrc->crc32) return &backup_ambrc;
    
    if (base_ambrc->version != CURRENT_AMBRC_VERSION) return &backup_ambrc;
    
    return base_ambrc;
}

static void ambrc_draw_base(struct VMemDesign* design) {
    design->x = 0;
    design->y = 0;

    // Status Bar
    vmem_printf(design, "AMBRC V%s [AOS Bootloader]", CURRENT_AMBRC_VERSION_STR);
    design->x = 0;
    design->y = 1;
    for (int i=0;i<IO_VMEM_MAX_COLS;i++) vmem_printc(design, 0xCD);

    // Tab Section
    design->x = 12;
    design->y = 2;
    for (int i=2;i<IO_VMEM_MAX_ROWS;i++) {vmem_printc(design, 0xBA); design->y = i; design->x = 12;}

    // Data Section
    design->x = IO_VMEM_MAX_COLS-15;
    design->y = 2;
    for (int i=2;i<IO_VMEM_MAX_ROWS;i++) {vmem_printc(design, 0xBA); design->y = i; design->x = IO_VMEM_MAX_COLS-15;}

    // Bottom Line
    design->x = 0;
    design->y = IO_VMEM_MAX_ROWS-1;
    for (int i=0;i<IO_VMEM_MAX_COLS;i++) vmem_printc(design, 0xCD);
}

static void ambrc_draw_tabs(struct ambrc* ambrc, struct VMemDesign* design, uint64_t selected) {
    const char* tabs[3] = {
        "Display",
        "Boot Info",
        "Kernel Info"
    };
    const size_t tabs_len[3] = {
        8,
        10,
        12
    };
    for (int i = 0; i < 3; i++) {
        design->x = 0;
        design->y = 2 + i;
        
        design->bg = selected == i ? ambrc->display.ambrc_selected_bg_color : ambrc->display.ambrc_bg_color;
        design->fg = selected == i ? ambrc->display.ambrc_selected_fg_color : ambrc->display.ambrc_fg_color;

        vmem_print(design, tabs[i]);
        for (int j=0; j<(13-tabs_len[i]);j++) vmem_printc(design, ' ');
    }
}

static const char* vmemc_to_str(enum VMemColors c) {
    switch (c) {
        case VMEM_COLOR_BLACK: return "Black";
        case VMEM_COLOR_BLUE: return "Blue";
        case VMEM_COLOR_GREEN: return "Green";
        case VMEM_COLOR_CYAN: return "Cyan";
        case VMEM_COLOR_RED: return "Red";
        case VMEM_COLOR_MAGENTA: return "Magenta";
        case VMEM_COLOR_BROWN: return "Brown";
        case VMEM_COLOR_LIGHT_GRAY: return "Light Gray";
        case VMEM_COLOR_DARK_GRAY: return "Dark Gray";
        case VMEM_COLOR_LIGHT_BLUE: return "Light Blue";
        case VMEM_COLOR_LIGHT_GREEN: return "Light Green";
        case VMEM_COLOR_LIGHT_CYAN: return "Light Cyan";
        case VMEM_COLOR_LIGHT_RED: return "Light Red";
        case VEM_COLOR_LIGHT_MAGENTA: return "Light Magenta";
        case VMEM_COLOR_YELLOW: return "Yellow";
        case VMEM_COLOR_WHITE: return "White";
        default: return "Unknown";
    }
}

static enum VMemColors str_to_vmemc(const char* str) {
    if (strcmp(str, "Black") == 0) return VMEM_COLOR_BLACK;
    if (strcmp(str, "Blue") == 0) return VMEM_COLOR_BLUE;
    if (strcmp(str, "Green") == 0) return VMEM_COLOR_GREEN;
    if (strcmp(str, "Cyan") == 0) return VMEM_COLOR_CYAN;
    if (strcmp(str, "Red") == 0) return VMEM_COLOR_RED;
    if (strcmp(str, "Magenta") == 0) return VMEM_COLOR_MAGENTA;
    if (strcmp(str, "Brown") == 0) return VMEM_COLOR_BROWN;
    if (strcmp(str, "Light Gray") == 0) return VMEM_COLOR_LIGHT_GRAY;
    if (strcmp(str, "Dark Gray") == 0) return VMEM_COLOR_DARK_GRAY;
    if (strcmp(str, "Light Blue") == 0) return VMEM_COLOR_LIGHT_BLUE;
    if (strcmp(str, "Light Green") == 0) return VMEM_COLOR_LIGHT_GREEN;
    if (strcmp(str, "Light Cyan") == 0) return VMEM_COLOR_LIGHT_CYAN;
    if (strcmp(str, "Light Red") == 0) return VMEM_COLOR_LIGHT_RED;
    if (strcmp(str, "Light Magenta") == 0) return VEM_COLOR_LIGHT_MAGENTA;
    if (strcmp(str, "Yellow") == 0) return VMEM_COLOR_YELLOW;
    if (strcmp(str, "White") == 0) return VMEM_COLOR_WHITE;
    return VMEM_COLOR_WHITE;
}

static const char* crash_verification_mode_to_str(uint8_t mode) {
    switch (mode) {
        case 0: return "None";
        case 1: return "Only TSC";
        case 2: return "Only Kernel";
        case 3: return "Both TSC and Kernel";
        default: return "Unknown";
    }
}

static void ambrc_draw_data(struct ambrc* ambrc, struct VMemDesign* design, uint64_t selected, uint64_t data_selected) {
    const uint8_t start_x = 14;
    const uint8_t start_y = 3;
    const uint8_t pane_width = (IO_VMEM_MAX_COLS - 15) - 14;

    design->bg = ambrc->display.ambrc_bg_color;
    design->fg = ambrc->display.ambrc_fg_color;

    switch (selected) {
        case 0: { // Display Tab
            const char* labels[] = {
                "BG Color",
                "FG Color",
                "Sel BG",
                "Sel FG",
                "AMBRC BG Color",
                "AMBRC FG Color",
                "AMBRC Sel BG",
                "AMBRC Sel FG",
                "Error FG",
                "Splash Duration"
            };
            const size_t label_lens[] = {
                9,
                9,
                7,
                7,
                15,
                15,
                13,
                13,
                9,
                16
            };
            const char* label_values[9];
            label_values[0] = vmemc_to_str(ambrc->display.bg_color);
            label_values[1] = vmemc_to_str(ambrc->display.fg_color);
            label_values[2] = vmemc_to_str(ambrc->display.selected_bg_color);
            label_values[3] = vmemc_to_str(ambrc->display.selected_fg_color);
            label_values[4] = vmemc_to_str(ambrc->display.ambrc_bg_color);
            label_values[5] = vmemc_to_str(ambrc->display.ambrc_fg_color);
            label_values[6] = vmemc_to_str(ambrc->display.ambrc_selected_bg_color);
            label_values[7] = vmemc_to_str(ambrc->display.ambrc_selected_fg_color);
            label_values[8] = vmemc_to_str(ambrc->display.error_fg_color);

            for (int i = 0; i < 10; i++) {
                design->x = start_x;
                design->y = start_y + i;
                
                if (i == data_selected) {
                    design->bg = ambrc->display.ambrc_selected_bg_color;
                    design->fg = ambrc->display.ambrc_selected_fg_color;
                } else {
                    design->bg = ambrc->display.ambrc_bg_color;
                    design->fg = ambrc->display.ambrc_fg_color;
                }

                if (i != 9) {
                    if (label_lens[i] + strlen(label_values[i]) + 3 > pane_width) {
                        if (label_lens[i] + strlen(label_values[i]) + 1 < pane_width)
                            vmem_printf(design, "%s:%s", labels[i], label_values[i]);
                        continue;
                    }
                    vmem_printf(design, "%s : %s", labels[i], label_values[i]);
                    for(int s = 0; s < (pane_width - (label_lens[i] + 3 + strlen(label_values[i]))); s++) vmem_printc(design, ' ');
                } else {
                    if (label_lens[i] + 5 > pane_width) {
                        if (label_lens[i] + 3 < pane_width)
                            vmem_printf(design, "%s:%u", labels[i], ambrc->display.splash_duration);
                        continue;
                    }
                    vmem_printf(design, "%s : %d", labels[i], ambrc->display.splash_duration);
                    for(int s = 0; s < (pane_width - (label_lens[i] + 5)); s++) vmem_printc(design, ' ');
                }
            }
            break;
        }

        case 1: { // Boot Info Tab
            const char* labels[] = {
                "Def OS Idx",
                "Safe OS Idx",
                "Panic OS Idx",
                "Crash Verif. Mode"
            };
            const size_t label_lens[] = {
                11,
                12,
                13,
                18
            };
            const uint32_t label_values[] = {
                ambrc->boot_info.default_os_idx,
                ambrc->boot_info.safe_os_idx,
                ambrc->boot_info.panic_os_idx,
                ambrc->boot_info.crash_verification_mode
            };

            for (int i = 0; i < 4; i++) {
                design->x = start_x;
                design->y = start_y + i;
                
                if (i == data_selected) {
                    design->bg = ambrc->display.ambrc_selected_bg_color;
                    design->fg = ambrc->display.ambrc_selected_fg_color;
                } else {
                    design->bg = ambrc->display.ambrc_bg_color;
                    design->fg = ambrc->display.ambrc_fg_color;
                }

                if (i != 3) {
                    if (label_lens[i] + 5 > pane_width) {
                        if (label_lens[i] + 3 < pane_width)
                            vmem_printf(design, "%s:%d", labels[i], label_values[i]);
                        continue;
                    }
                    vmem_printf(design, "%s : %d", labels[i], label_values[i]);
                    for(int s = 0; s < (pane_width - (label_lens[i] + 5)); s++) vmem_printc(design, ' ');
                } else {
                    const char* mode = crash_verification_mode_to_str(label_values[i]);
                    size_t mode_len = strlen(mode);
                    if (label_lens[i] + mode_len + 3 > pane_width) {
                        if (label_lens[i] + mode_len + 1 < pane_width)
                            vmem_printf(design, "%s:%s", labels[i], mode);
                        continue;
                    }
                    vmem_printf(design, "%s : %s", labels[i], mode);
                    for(int s = 0; s < (pane_width - (label_lens[i] + mode_len + 3)); s++) vmem_printc(design, ' ');
                }
            }
            break;
        }

        case 2: { // Kernel Info Tab
            const char* labels[3] = {
                "Selected Kernel",
                "Load Address",
                "Entry Point"
            };
            const size_t label_lens[3] = {
                16,
                11,
                12,
            };
            const uint32_t label_values[3] = {
                active_k_idx,
                ambrc->kernel_info[active_k_idx].load_addr,
                ambrc->kernel_info[active_k_idx].entry_point
            };

            for (int i = 0; i < 3; i++) {
                design->x = start_x;
                design->y = start_y + i;
                
                if (i == data_selected) {
                    design->bg = ambrc->display.ambrc_selected_bg_color;
                    design->fg = ambrc->display.ambrc_selected_fg_color;
                } else {
                    design->bg = ambrc->display.ambrc_bg_color;
                    design->fg = ambrc->display.ambrc_fg_color;
                }

                if (i > 0) {
                    if (label_lens[i] + 6 + 3 > pane_width) {
                        if (label_lens[i] + 6 + 1 < pane_width)
                            vmem_printf(design, "%s:0x%lx", labels[i], label_values[i]);
                        continue;
                    }
                    vmem_printf(design, "%s : 0x%lx", labels[i], label_values[i]);
                    for(int s = 0; s < (pane_width - (label_lens[i] + 6 + 3)); s++) vmem_printc(design, ' ');
                } else {
                    if (label_lens[i] + 2 + 3 > pane_width) {
                        if (label_lens[i] + 2 + 1 < pane_width)
                            vmem_printf(design, "%s:%d", labels[i], label_values[i]);
                        continue;
                    }
                    vmem_printf(design, "%s : %d", labels[i], label_values[i]);
                    for(int s = 0; s < (pane_width - (label_lens[i] + 2 + 3)); s++) vmem_printc(design, ' ');
                }
            }
            break;
        }
        
        default: return;
    }
}

static void ambrc_reset_tab(struct ambrc* ambrc, uint64_t tab) {
    switch (tab) {
        case 0: // Reset Display
            memcpy(&ambrc->display, &def_ambrc.display, sizeof(struct ambrc_display));
            break;
        case 1: // Reset Boot Info
            memcpy(&ambrc->boot_info, &def_ambrc.boot_info, sizeof(struct ambrc_boot_info));
            break;
        case 2: // Reset Current Kernel Info
            memcpy(&ambrc->kernel_info[active_k_idx], &def_ambrc.kernel_info[active_k_idx], sizeof(struct ambrc_kernel_info));
            break;
        default: break;
    }
}

static void ambrc_display_popup(struct ambrc* ambrc, struct VMemDesign* design, const char* popup_msg) {
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

    design->bg = ambrc->display.ambrc_bg_color;
    design->fg = ambrc->display.ambrc_fg_color;
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

static void ambrc_set_data(struct ambrc* ambrc, uint64_t tab, uint64_t row, int8_t dir) {
    switch (tab) {
        case 0: { // Display Tab - Cycle Colors
            if (row == 9) {
                if (dir > 0) ambrc->display.splash_duration++;
                else if (dir < 0 && ambrc->display.splash_duration > 0) ambrc->display.splash_duration--;
                break;
            }
            enum VMemColors* color_ptr = (enum VMemColors*)&ambrc->display;
            color_ptr[row] = (enum VMemColors)((color_ptr[row] + dir) & 0xF); // Wrap 0-15
            break;
        }
        case 1: { // Boot Info Tab - Increment/Decrement Indexes
            uint16_t* val_ptr;
            uint64_t max = AMBRC_MAX_KERNELS;
            switch (row) {
                case 0: val_ptr = &ambrc->boot_info.default_os_idx; break;
                case 1: val_ptr = &ambrc->boot_info.safe_os_idx; break;
                case 2: val_ptr = &ambrc->boot_info.panic_os_idx; break;
                case 3: val_ptr = &ambrc->boot_info.crash_verification_mode; max = 3; break;
                default: return;
            }
            
            // Bounds check
            if (dir > 0 && *val_ptr < max) (*val_ptr)++;
            else if (dir < 0 && *val_ptr > 0) (*val_ptr)--;
            break;
        }
        case 2: { // Kernel Info Tab - Take input
            switch(row) {
                case 0: // "Selected Kernel" Row
                    if (dir > 0 && active_k_idx < AMBRC_MAX_KERNELS - 1) active_k_idx++;
                    else if (dir < 0 && active_k_idx > 0) active_k_idx--;
                    break;
                
                case 1: // Load Address
                    ambrc->kernel_info[active_k_idx].load_addr += (dir * 0x1000); 
                    break;

                case 2: // Entry Point
                    ambrc->kernel_info[active_k_idx].entry_point += (dir * 0x1000);
                    break;
                default: return;
            }
            break;
        }
        default: break;
    }
    changes_made = 1;
}

static void ambrc_handle_data(struct ambrc* ambrc, struct VMemDesign* design, uint64_t tab, uint64_t* row, uint8_t scancode) {
    switch (scancode) {
        case 0x48: { // Up Arrow
            if (*row > 0) (*row)--;
            break;
        }
        case 0x50: { // Down Arrow
            // Bounds check based on tab
            uint64_t max_row = (tab == 0) ? 9 : (tab == 1) ? 3 : (AMBRC_MAX_KERNELS - 1);
            if (*row < max_row) (*row)++;
            break;
        }
        case 0x4D: { // Right Arrow (Increase/Next)
            ambrc_set_data(ambrc, tab, *row, 1);
            break;
        }
        case 0x4B: { // Left Arrow (Decrease/Prev)
            ambrc_set_data(ambrc, tab, *row, -1);
            break;
        }
        case 0x13: { // R
            ambrc_display_popup(ambrc, design, "Are you sure you want to reset? (y/N)");
            uint8_t valid = 0;
            while (!valid) {
                uint8_t scancode = ps2_read_scan();
                switch (scancode) {
                    case 0x15: valid=1; break; // 'Y' key
                    case 0x31: // 'N' key
                    case 0x01: // ESC also acts as no
                        vmem_clear_screen(design);
                        ambrc_draw_base(design);
                        ambrc_draw_tabs(ambrc, design, tab);
                        ambrc_draw_data(ambrc, design, tab, *row);
                        return;
                    default: valid=0; break;
                }
            }
            ambrc_reset_tab(ambrc, tab);
            vmem_clear_screen(design);
            ambrc_draw_base(design);
            ambrc_draw_tabs(ambrc, design, tab);
            ambrc_draw_data(ambrc, design, tab, *row);
            return;
        }
        default: return;
    }
    ambrc_draw_data(ambrc, design, tab, *row);
}

static void ambrc_handle_changes(struct ambrc* ambrc, struct VMemDesign* design, uint8_t* running) {
    if (!changes_made) return;
    ambrc_display_popup(ambrc, design, "Save changes to disk? (y/n/C)");

    uint8_t valid = 0;
    while (!valid) {
        uint8_t scancode = ps2_read_scan();
        switch (scancode) {
            case 0x15: valid=1; break; // 'Y' key
            case 0x31: *running=0; return; // 'N' key
            case 0x2E: return; // 'C' key
            case 0x01: return; // ESC also acts as cancel
            default: valid=0; break;
        }
    }

    ambrc->crc32 = 0;
    ambrc->crc32 = calculate_crc32((const uint8_t*)ambrc, sizeof(struct ambrc));
    gdrive->write_blk(gdrive->cur_port, 2046, 2, ambrc);
    *running = 0;
}

void start_ambrc(struct drive_device* drive) {
    gdrive = drive;
    struct ambrc* ambrc = get_ambrc();
    struct VMemDesign design_raw = {
        .x = 0,
        .y = 0,
        .bg = ambrc->display.ambrc_bg_color,
        .fg = ambrc->display.ambrc_fg_color,
        .serial_out=0
    };
    struct VMemDesign* design = &design_raw;

    vmem_disable_cursor();

    // Runtime loop
    uint64_t selected = 0;
    uint8_t within_data = 0;
    uint64_t data_selected = 0;
    vmem_clear_screen(design);
    ambrc_draw_base(design);
    ambrc_draw_tabs(ambrc, design, selected);

    uint8_t running = 1;
    while (running) {
        uint8_t scancode = ps2_read_scan(); 
        if (within_data) {
            switch(scancode) {
                case 0x01: { // ESC
                    within_data = 0;
                    data_selected = 0;
                    vmem_clear_screen(design);
                    ambrc_draw_base(design);
                    ambrc_draw_tabs(ambrc, design, selected);
                    break;
                }
                default: {
                    ambrc_handle_data(ambrc, design, selected, &data_selected, scancode);
                    break;
                }
            }
        } else {
            switch (scancode) {
                case 0x48: { // Up Arrow
                    if(selected > 0) ambrc_draw_tabs(ambrc, design, --selected);
                    break;
                }
                case 0x50: { // Down Arrow
                    if(selected < 2) ambrc_draw_tabs(ambrc, design, ++selected);
                    break;
                }
                case 0x1C: { // Enter
                    within_data = 1;
                    data_selected = 0;
                    ambrc_draw_data(ambrc, design, selected, data_selected);
                    break;
                }
                case 0x01: { // ESC
                    if (!changes_made) {running = 0; continue;}
                    ambrc_handle_changes(ambrc, design, &running);
                    if (running) {
                        vmem_clear_screen(design);
                        ambrc_draw_base(design);
                        ambrc_draw_tabs(ambrc, design, selected);
                    }
                    break;
                }
                default: break;
            }
        }
    }
}