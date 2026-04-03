#include <system.h>
#include <asm.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/io/drive.h>
#include <inc/drivers/keyboard/keyboard.h>
#include <inc/core/acpi.h>

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

static char cmd[1024] = {0};
static int cmd_len = 0;

static uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

static void execute_cmd(struct ambrc* ambrc, struct VMemDesign* design, uint8_t* running) {
    if (cmd_len == 0) return;

    char* c = cmd;
    while (*c == ' ') c++;
    if (*c == '\0') return;

    design->y++;
    design->x = 0;
    switch (c[0]) {
        case 'a':
            if (!strcmp(c, "about")) {
                vmem_print(design, "\nAOS Bootloader Panic Shell\n");
            } else if (!strcmp(c, "acpi")) {
                vmem_print(design, "\nACPI Info Loaded\n");
            }
            break;

        case 'b':
            if (!strcmp(c, "beep")) {
                asm_outb(0x61, asm_inb(0x61) | 3);
            }
            break;

        case 'c':
            if (!strcmp(c, "clear")) {
                vmem_clear_screen(design);
                design->x = 0;
                design->y = 0;
            } else if (!strcmp(c, "cpu")) {
                vmem_print(design, "\nCPU Info OK\n");
            }
            break;

        case 'd':
            if (!strcmp(c, "dump")) {
                vmem_print(design, "\nMemory dump not implemented\n");
            } else if (!strcmp(c, "drives")) {
                vmem_print(design, "\nListing drives...\n");
            }
            break;

        case 'e':
            if (!strcmp(c, "echo")) {
                char* msg = c + 5;
                vmem_print(design, "\n");
                vmem_print(design, msg);
                vmem_print(design, "\n");
            } else if (!strcmp(c, "exit")) {
                *running = 0;
            }
            break;

        case 'h':
            if (!strcmp(c, "help")) {
                
            }
            break;

        case 'i':
            if (!strcmp(c, "info")) {
                aos_sysinfo_t* sysinfo = AOS_SYS_INFO_LOC;
                vmem_printf(design, "Sysinfo:\n");
                vmem_printf(design, "Sysinfo:\n");
                vmem_printf(design, "--------------------------------\n");

                vmem_printf(design, "Boot Drive      : 0x%02x\n", sysinfo->boot_drive);

                vmem_printf(design, "Boot Mode       : ");
                switch (sysinfo->boot_mode) {
                    case 0: vmem_print(design, "Normal\n"); break;
                    case 1: vmem_print(design, "Recovery\n"); break;
                    case 2: vmem_print(design, "Shell\n"); break;
                    case 3: vmem_print(design, "VGA\n"); break;
                    case 4: vmem_print(design, "VGA+Shell\n"); break;
                    default: vmem_print(design, "Unknown\n"); break;
                }

                vmem_printf(design, "CPU Vendor      : %s\n", sysinfo->cpu_vendor);
                vmem_printf(design, "CPU Signature   : 0x%08x\n", sysinfo->cpu_signature);

                vmem_printf(design, "APIC Present    : %s\n", sysinfo->apic_present ? "Yes" : "No");

                vmem_printf(design, "TSC Frequency   : %lu Hz\n", sysinfo->tsc_freq_hz);

                vmem_printf(design, "TSC (MHz)       : %lu MHz\n", sysinfo->tsc_freq_hz / 1000000);

                aos_sysinfo_t sysinfo_cpy = *sysinfo;
                uint8_t sum = compute_checksum((uint8_t*)&sysinfo_cpy, sizeof(aos_sysinfo_t));

                vmem_printf(design, "Checksum Stored : 0x%02x\n", sysinfo->checksum);
                vmem_printf(design, "Checksum Calc   : 0x%02x\n", sum);
                vmem_printf(design, "Checksum Valid  : %s\n", (sum == sysinfo->checksum) ? "Yes" : "No");

                vmem_printf(design, "--------------------------------\n");
            }
            break;

        case 'm':
            if (!strcmp(c, "mem")) {
                vmem_print(design, "\nMemory OK\n");
            }
            break;

        case 'r':
            if (!strcmp(c, "reboot")) {
                vmem_print(design, "\nRebooting...\n");
                acpi_reboot();
            }
            break;

        case 's':
            if (!strcmp(c, "shutdown")) {
                vmem_print(design, "\nShutdown (hlt)\n");
                for (;;) asm("hlt");
            }
            break;

        case 't':
            if (!strcmp(c, "time")) {
                vmem_print(design, "\nRTC not implemented\n");
            }
            break;

        case 'v':
            if (!strcmp(c, "version")) {
                vmem_print(design, "\nAOS Panic Shell v1.0\n");
            }
            break;

        case 'w':
            if (!strcmp(c, "whoami")) {
                vmem_print(design, "\naos-panic-shell-user\n");
            }
            break;

        default:
            vmem_print(design, "\nUnknown command\n");
            break;
    }
}

void start_panic_shell(void) {
    struct ambrc* ambrc = get_ambrc();

    size_t start_x = (IO_VMEM_MAX_COLS / 2) - 8;
    size_t start_y = 0;
    
    size_t tstart_x = 0;
    size_t tstart_y = 2;
    size_t t_w = IO_VMEM_MAX_COLS;
    size_t t_h = IO_VMEM_MAX_ROWS - 2;

    struct VMemDesign design_raw = {
        .bg=ambrc->display.bg_color,
        .fg=ambrc->display.fg_color,
        .x=start_x,
        .y=start_y,
        .serial_out=0
    };
    struct VMemDesign* design = &design_raw;

    vmem_clear_screen(design);
    vmem_disable_cursor();
    design->x = start_x;
    design->y = start_y;

    vmem_print(design, "AOS Panic Shell");

    design->x = tstart_x;
    design->y = tstart_y;
    vmem_print(design, "$> ");

    uint8_t running = 1;
    uint64_t cmd_start_x = tstart_x + 3;
    uint64_t cmd_start_y = tstart_y;
    while (running) {
        char c = keyboard_ps2_get_char();
        if (!((c >= 32 && c <= 126) || c == '\n' || c == '\b')) continue;
        switch (c) {
            case '\b': {
                cmd[--cmd_len] = ' ';
                break;
            }
            case '\n': {
                execute_cmd(ambrc, design, &running);
                if (!running) continue;
                design->x = tstart_x;
                design->y++;
                vmem_print(design, "$> ");
                cmd_start_x = tstart_x + 3;
                cmd_start_y = design->y;
                
                memset(cmd, 0, sizeof(cmd));
                cmd_len = 0;

                continue;
            }
            default: {
                if (cmd_len >= sizeof(cmd) - 1) break;
                cmd[cmd_len++] = c;
                
                cmd[cmd_len] = '\0';
                break;
            }
        }
        char* cptr = cmd;
        size_t w = 0;
        design->x = cmd_start_x;
        design->y = cmd_start_y;
        while (*cptr) {
            if (w == t_w) {
                design->x = tstart_x;
                if (++design->y > t_h) {
                    vmem_clear_screen(design);
                    design->x = start_x;
                    design->y = start_y;
                    vmem_print(design, "AOS Panic Shell");
                    design->x = tstart_x;
                    design->y = tstart_y;
                }
            }
            vmem_printc(design, *cptr);
            cptr++;
            w++;
        }
    }
}