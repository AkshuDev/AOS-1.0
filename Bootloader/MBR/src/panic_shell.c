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

static struct drive_device* cdrive = NULL;

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
    uint8_t valid = 0;
    switch (c[0]) {
        case 'a':
            if (!strcmp(c, "about")) {
                vmem_print(design, "\nAOS Bootloader Panic Shell\nA place where you can debug crashes and fix them!\n");
                valid = 1;
            } else if (!strcmp(c, "acpi")) {
                aos_sysinfo_t* sysinfo = AOS_SYS_INFO_LOC;
                vmem_print(design, "\nUsing ACPI, Info:\n");
                vmem_print(design, sysinfo->apic_present == 1 ? "\tAPIC: Available\n" : "\tAPIC: Unavailable\n");
                valid = 1;
            }
            break;

        case 'c':
            if (!strcmp(c, "clear")) {
                vmem_clear_screen(design);
                design->x = 0;
                design->y = 0;
                valid = 1;
            } else if (!strcmp(c, "cpu")) {
                uint32_t eax, ebx, ecx, edx;
                asm volatile (
                    "cpuid"
                    : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                    : "a"(0), "c"(0)
                );

                char vendor[13];
                *(uint32_t*)&vendor[0] = ebx;
                *(uint32_t*)&vendor[4] = edx;
                *(uint32_t*)&vendor[8] = ecx;
                vendor[12] = '\0';

                vmem_printf(design, "\nCPU Vendor: %s\n", vendor);

                char brand[49];
                for (int i = 0; i < 3; i++) {
                    uint32_t eax, ebx, ecx, edx;
                    asm volatile (
                        "cpuid"
                        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                        : "a"(0x80000002 + i), "c"(0)
                    );
                    *(uint32_t*)&brand[i*16 + 0] = eax;
                    *(uint32_t*)&brand[i*16 + 4] = ebx;
                    *(uint32_t*)&brand[i*16 + 8] = ecx;
                    *(uint32_t*)&brand[i*16 + 12] = edx;
                }
                brand[48] = '\0';

                vmem_printf(design, "CPU Brand: %s\n", brand);
                valid = 1;
            }
            break;

        case 'd':
            if (!strcmp(c, "drives")) {
                vmem_print(design, "\nListing drives...\n");
                if (cdrive->active != 1) vmem_print(design, "No drives found!\n");
                else {
                    vmem_printf(design, "Drive %s:\n", cdrive->name);
                    vmem_printf(design, "\tCurrent Port: %d\n", cdrive->cur_port);
                    vmem_printf(design, "\tBlock Count: %lu\n", cdrive->block_dev.block_count);
                    vmem_printf(design, "\tBlock Size: %lu bytes\n", cdrive->block_dev.block_size);
                    uint64_t tsize_bytes = cdrive->block_dev.block_size * cdrive->block_dev.block_count;
                    uint64_t tsize = tsize_bytes;
                    const char* size_type = "Bytes";
                    if (tsize_bytes < 1048576 && tsize_bytes > 1024) {
                        tsize = tsize_bytes / 1024;
                        size_type = "KB";
                    } else if (tsize_bytes < 1073741824 && tsize_bytes > 1048576) {
                        tsize = tsize_bytes / 1048576;
                        size_type = "MB";
                    } else if (tsize_bytes < (1073741824 * 1024) && tsize_bytes > 1073741824) {
                        tsize = tsize_bytes / (1073741824 * 1024);
                        size_type = "GB";
                    } else if (tsize_bytes < (1073741824 * 1024 * 1024) && tsize_bytes > (1073741824 * 1024)) {
                        tsize = tsize_bytes / (1073741824 * 1024 * 1024);
                        size_type = "TB";
                    } else if (tsize_bytes < (1073741824 * 1024 * 1024 * 1024) && tsize_bytes > (1073741824 * 1024 * 1024)) {
                        tsize = tsize_bytes / (1073741824 * 1024 * 1024 * 1024);
                        size_type = "PB";
                    } else {
                        tsize = tsize_bytes / (1073741824 * 1024 * 1024 * 1024 * 1024);
                        size_type = "EB";
                    }
                    vmem_printf(design, "\tTotal Size: %lu %s\n", tsize, size_type);
                    vmem_printf(design, "\n\tDevice ID: %u\n", cdrive->pcie_device->device_id);
                    vmem_printf(design, "\tVendor ID: %u\n", cdrive->pcie_device->vendor_id);
                    vmem_printf(design, "\tClass Code: %u\n", cdrive->pcie_device->class_code);
                    vmem_printf(design, "\tSubclass Code: %u\n", cdrive->pcie_device->subclass);
                    vmem_printf(design, "\tProgramming Interface: %u\n", cdrive->pcie_device->prog_if);
                    vmem_printf(design, "\tBus: %u\n", cdrive->pcie_device->bus);
                    vmem_printf(design, "\tSlot: %u\n", cdrive->pcie_device->slot);
                    vmem_printf(design, "\tFunc: %u\n", cdrive->pcie_device->func);
                }

                valid = 1;
            }
            break;

        case 'e':
            if (!strcmp(c, "echo")) {
                char* msg = c + 5;
                vmem_print(design, "\n");
                vmem_print(design, msg);
                vmem_print(design, "\n");
                valid = 1;
            } else if (!strcmp(c, "exit")) {
                *running = 0;
                valid = 1;
            }
            break;

        case 'h':
            if (!strcmp(c, "help")) {
                vmem_print(design, "\n--- AOS Panic Shell Help ---\n");
                
                vmem_print(design, "System Commands:\n");
                vmem_print(design, "\tinfo     - Display AOS system information & checksums\n");
                vmem_print(design, "\tacpi     - Show ACPI/APIC status\n");
                vmem_print(design, "\treboot   - Restart the computer via ACPI\n");
                vmem_print(design, "\tshutdown - Halt the CPU\n");
                vmem_print(design, "\texit     - Close the panic shell\n");

                vmem_print(design, "\nHardware Debugging:\n");
                vmem_print(design, "\tcpu      - Identify CPU vendor and brand string\n");
                vmem_print(design, "\tdrives   - List active storage devices and PCIe info\n");
                vmem_print(design, "\tmemtest  - Perform a basic R/W test on 0x100000\n");

                vmem_print(design, "\nUtilities:\n");
                vmem_print(design, "\tabout    - What is this shell?\n");
                vmem_print(design, "\tclear    - Clear the terminal screen\n");
                vmem_print(design, "\techo     - Print text to the screen\n");
                vmem_print(design, "\tversion  - Show shell version\n");
                vmem_print(design, "\twhoami   - Show current user\n");
                
                vmem_print(design, "----------------------------\n");
                valid = 1;
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

                valid = 1;
            }
            break;

        case 'm':
            if (!strcmp(c, "memtest")) {
                volatile uint32_t* addr = (uint32_t*)0x100000;
                uint8_t passed = 1;

                for (int i = 0; i < 4; i++) {
                    *(addr + i) = 0xAA;
                }

                for (int i = 0; i < 4; i++) {
                    if (*(addr + i) != 0xAA) {
                        vmem_print(design, "\nMemory Test 1: Failed\n");
                        passed = 0;
                    }
                }

                if (passed) vmem_print(design, "Memory Test 1: Passed\n");
                else passed = 1; // Reset

                for (int i = 0; i < 4; i++) {
                    *(addr + i) = 0x55;
                }

                for (int i = 0; i < 4; i++) {
                    if (*(addr + i) != 0x55) {
                        vmem_print(design, "Memory Test 2: Failed\n");
                        passed = 0;
                    }
                }

                if (passed) vmem_print(design, "Memory Test 2: Passed\n");
                valid = 1;
            }
            break;

        case 'r':
            if (!strcmp(c, "reboot")) {
                vmem_print(design, "\nRebooting...\n");
                valid = 1;
                acpi_reboot();
            }
            break;

        case 's':
            if (!strcmp(c, "shutdown")) {
                vmem_print(design, "\nShutdown (hlt)\n");
                valid = 1;
                for (;;) asm("hlt");
            }
            break;

        case 'v':
            if (!strcmp(c, "version")) {
                vmem_print(design, "\nAOS Panic Shell v1.0\n");
                valid = 1;
            }
            break;

        case 'w':
            if (!strcmp(c, "whoami")) {
                vmem_print(design, "\naos-panic-shell-user\n");
                valid = 1;
            }
            break;

        default:
            break;
    }

    if (!valid) vmem_printf(design, "\nUnknown command: %s\n", c);
}

void start_panic_shell(struct drive_device* current_drive) {
    cdrive = current_drive;
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
                if (cmd_len == 0) break;
                cmd[--cmd_len] = ' ';
                if (design->x > cmd_start_x) {
                    design->x--;
                    vmem_printc(design, ' ');
                    design->x--;
                }
                break;
            }
            case '\n': {
                execute_cmd(ambrc, design, &running);
                if (!running) continue;

                memset(cmd, 0, sizeof(cmd));
                cmd_len = 0;

                design->x = tstart_x;
                design->y++;

                if (design->y >= tstart_y + t_h) {
                    vmem_scroll_up(design, tstart_y, tstart_y + t_h, t_w);
                    design->y = tstart_y + t_h - 1;
                }

                vmem_print(design, "$> ");
                cmd_start_x = design->x;
                cmd_start_y = design->y;
                break;
            }
            default: {
                if (cmd_len >= sizeof(cmd) - 1) break;

                cmd[cmd_len++] = c;
                cmd[cmd_len] = '\0';
                vmem_printc(design, c);

                if (design->x >= t_w) {
                    design->x = 0;
                    design->y++;
                    if (design->y >= tstart_y + t_h) {
                        vmem_scroll_up(design, tstart_y, tstart_y + t_h, t_w);
                        design->y = tstart_y + t_h - 1;
                    }
                }
                break;
            }
        }
    }
}