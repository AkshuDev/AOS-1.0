#include <system.h>
#include <inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/core/acpi.h>
#include <inc/core/idt.h>
#include <inc/drivers/io/io.h>
#include <inc/core/pcie.h>
#include <inc/core/smp.h>

#include <inc/drivers/io/drive.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#include <PBFS/headers/pbfs.h>
#include <PBFS/headers/pbfs_structs.h>
#undef PBFS_NDRIVERS

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#define SHELL_MAX_INPUT 128

static char* help_shell = "Usage: [COMMAND]\n"
    "Commands:\n"
    "\thelp: Provides this output.\n"
    "\techo [msg]: Prints [msg] on stdout.\n"
    "\treboot: Reboots the system.\n"
    "\tclear: Clears the screen.\n"
    "\tshutdown: Shuts down the system.\n"
    "\tstart <program>: Starts a Program, Use 'start -help' for more info\n";

static int help_shell_nlines = 8;

static drive_device_t current_drive = {0};
static uint8_t current_drive_works = 0;
static uint8_t current_drive_mounted = 0;

static struct pbfs_funcs pbfs_init_funcs = {
    .malloc = kmalloc,
    .free = kfree
};
static char g_pbfs_cwd[PBFS_MAX_PATH_LEN] = {0};
static struct pbfs_mount g_pbfs_mnt = {0};

// Define a static stack array
void kernel_main(void) __attribute__((used, noinline, section(".start"), noreturn));
void aos_shell_pm(void);
void exec_cmd(char* cmd, int* lines, struct VMemDesign* vmem_design);
static void cmd_print_help(struct VMemDesign* vmem_design, int* lines);
void cmd_start(char* program, int* lines, struct VMemDesign* vmem_design);
void aospp_start();

extern uint64_t stack_top; // From linker script

void kernel_main(void) {
    asm volatile (
        "mov %0, %%rsp\n\t"
        "mov %%rsp, %%rbp"
        :
        :
        "r"(&stack_top)
        :
        "memory"
    );

    serial_init();
    idt_init();
    // Reserve MMIO region at 0xF0000000, kernel starts at 0x100000
    serial_print("AOS++ LOADED!\n");
    pager_init(); // Inits AVMF Too

    acpi_init();
    if (pcie_init() == 0) {
        serial_print("[AOS] PCIe Initialization failed! Shutting Down!\n");
        for (;;) asm("hlt");
    }

    ktimer_calibrate();
    smp_init();

    // Search for drives
    current_drive_works = 0;
    if (get_available_drives(&current_drive) != 1) {
        serial_print("[AOS] Failed to find any I/O Drives!\n");
    } else {
        if (current_drive.active != 1) {
            if (current_drive.init != NULL) current_drive.init();
        }
        // else already initialized by the func
        // Print info
        serial_printf("[AOS] Found I/O Drive ->\n\tName: %s\n\tBlock Size: %u\n\tTotal Blocks: %u\n", current_drive.name, current_drive.block_dev.block_size, current_drive.block_dev.block_count);
        // do test read for now
        char tmp[512];
        uint8_t read = 0;
        if (current_drive.read_blk != NULL) {
            if (current_drive.read_blk(current_drive.cur_port, 0, 1, tmp) == 1)
                read = 1;
        }
        if (read == 0) {
            serial_print("[AOS] Test read from drive failed! Scrapping drive!\n");
        } else {
            serial_print("[AOS] Drive passed the tests, using drive!\n");
            current_drive_works = 1;
        }
    }

    pbfs_init(&pbfs_init_funcs);

    // Now safe to use local variables
    struct VMemDesign vmem_design = {
        .x = 0,
        .y = 0,
        .fg = VMEM_COLOR_WHITE,
        .bg = VMEM_COLOR_BLACK,
        .serial_out = 1
    };

    vmem_set_cursor(0, 0);
    vmem_clear_screen(&vmem_design);
    vmem_print(&vmem_design, "Welcome To AOS!\n\n");
    aos_shell_pm();
    for (;;) asm("hlt");
}

void aos_shell_pm(void) {
    struct VMemDesign vmem_design = {
        .x = 0,
        .y = 0,
        .fg = VMEM_COLOR_WHITE,
        .bg = VMEM_COLOR_BLACK,
        .serial_out = 1
    };
    
    char input[SHELL_MAX_INPUT];
    int lines = 1;

    // Read sysinfo
    uint8_t boot_drive = *AOS_BOOT_INFO_LOC;
    aos_sysinfo_t SystemInfo = *AOS_SYS_INFO_LOC;
    vmem_print(&vmem_design, "SystemInfo:\n");
    vmem_printf(&vmem_design, "Boot Drive: 0x%llx (%llu)\nBoot Mode: 0x%llx (%llu)\n", (uint64_t)SystemInfo.boot_drive, (uint64_t)SystemInfo.boot_drive, (uint64_t)SystemInfo.boot_mode, (uint64_t)SystemInfo.boot_mode);
    vmem_printf(&vmem_design, "CPU Vendor: %s\n", (uintptr_t)&SystemInfo.cpu_vendor);
    lines += 7;

    while (1) {
        vmem_printf(&vmem_design, "AOS: %s $> ", g_pbfs_cwd);
        ps2_read_line(input, SHELL_MAX_INPUT, &vmem_design);

        exec_cmd(input, &lines, &vmem_design);
    }
}

void exec_cmd(char* cmd, int* lines, struct VMemDesign* vmem_design) {
    if (strcmp(cmd, "help") == 0) {
        vmem_print(vmem_design, help_shell);
        *lines += help_shell_nlines;
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        vmem_print(vmem_design, cmd + 5);
        vmem_print(vmem_design, "\n");
        *lines += 1;
    } else if (strcmp(cmd, "reboot") == 0) {
        vmem_print(vmem_design, "Rebooting...\n");
        pager_destroy_table(4);
        acpi_reboot();
    } else if (strcmp(cmd, "clear") == 0) {
        vmem_clear_screen(vmem_design);
        *lines = 0;
    } else if (strncmp(cmd, "start ", 6) == 0) {
        cmd_start(cmd + 6, lines, vmem_design);
    } else if (strcmp(cmd, "pbfsctl-fmt") == 0) {
        if (current_drive_works != 1) {
            vmem_print(vmem_design, "Error: Current Drive doesn't work!\n");
            *lines += 1;
        } else {
            int out = pbfs_format(&current_drive.block_dev, 1, 100, current_drive.cur_port);
            if (out != PBFS_RES_SUCCESS) {
                vmem_printf(vmem_design, "Error: Failed! (Code: %d)\n", out);
                *lines += 1;
            }
            current_drive_mounted = 0;
        }
    } else if (strcmp(cmd, "pbfsctl-mnt") == 0) {
        if (current_drive_works != 1) {
            vmem_print(vmem_design, "Error: Current Drive doesn't work!\n");
            *lines += 1;
        } else {
            int out = pbfs_mount(&current_drive.block_dev, &g_pbfs_mnt);
            if (out != PBFS_RES_SUCCESS) {
                vmem_printf(vmem_design, "Error: Failed! (Code: %d)\n", out);
                *lines += 1;
            }
            current_drive_mounted = 0;
        }
    } else if (strncmp(cmd, "cd ", 3) == 0) {
        if (current_drive_works == 0) {
            vmem_print(vmem_design, "Error: Current Drive doesn't work!\n");
            *lines += 1;
        } else {
            if (current_drive_mounted == 0) {
                vmem_print(vmem_design, "Error: Current Drive isn't mounted!\n");
                *lines += 1;
            } else {
                char* _path = cmd + 3;
                char path[PBFS_MAX_PATH_LEN];
                path_normalize(_path, path, PBFS_MAX_PATH_LEN);

                PBFS_DMM_Entry entry;
                uint64_t tmplba = 0;
                if (pbfs_find_entry(path, &entry, &tmplba, &g_pbfs_mnt) != PBFS_RES_SUCCESS || !(entry.type & METADATA_FLAG_DIR)) {
                    vmem_print(vmem_design, "Error: No such directory!\n");
                    *lines += 1;
                } else {
                    strcpy(g_pbfs_cwd, path);
                }
            }
        }
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (current_drive_works == 0) {
            vmem_print(vmem_design, "Error: Current Drive doesn't work!\n");
            *lines += 1;
        } else {
            if (current_drive_mounted == 0) {
                vmem_print(vmem_design, "Error: Current Drive isn't mounted!\n");
                *lines += 1;
            } else {
                char* _path = cmd + 3;
                char path[PBFS_MAX_PATH_LEN];
                path_normalize(_path, path, PBFS_MAX_PATH_LEN);

                PBFS_DMM_Entry entry;
                uint64_t tmplba = 0;
                if (pbfs_find_entry(path, &entry, &tmplba, &g_pbfs_mnt) == PBFS_RES_SUCCESS) {
                    vmem_print(vmem_design, "Error: File or Directory already exists!\n");
                    *lines += 1;
                } else {
                    int out = pbfs_add_dir(&g_pbfs_mnt, path, 0, 0, (PBFS_Permission_Flags)(PERM_READ | PERM_WRITE));
                    if (out != PBFS_RES_SUCCESS) {
                        vmem_printf(vmem_design, "Error: Failed! (Code: %d)\n", out);
                        *lines += 1;
                    }
                }
            }
        }
    } else {
        vmem_print(vmem_design, "Unknown command\n");
        *lines += 1;
    }
    *lines += 1;
    if (*lines > 25) {
        vmem_clear_screen(vmem_design);
        *lines = 0;
    }
}

void cmd_print_help(struct VMemDesign* vmem_design, int* lines) {
    const char* help = "Usage: start <program | -[OPTIONS]>\n"
        "OPTIONS:\n"
        "\thelp: Prints this help message\n"
        "Available Programs:\n"
        "\taos++/aospp: AfterGreat Operating System ++ (Enters GUI Mode)\n";
    vmem_print(vmem_design, help);
    *lines += 6;
}

void cmd_start(char* program, int* lines, struct VMemDesign* vmem_design) {
    if (strcmp(program, "-help") == 0) {
        cmd_print_help(vmem_design, lines);
    } else if (strcmp(program, "aospp") == 0 || strcmp(program, "aos++") == 0) {
        vmem_print(vmem_design, "Starting AOS++\n");
        *lines += 1;
        aospp_start();
        vmem_print(vmem_design, "AOS++ Failed to load!\n");
        *lines += 1;
    } else {
        vmem_print(vmem_design, "Unknown Program: ");
        vmem_print(vmem_design, program);
        vmem_print(vmem_design, "\n");
        *lines += 1;
    }
}

void aos_vmss_start(void) {
    asm("int $0x51");

    struct VMemDesign vmem_design = {
        .x = 0,
        .y = 0,
        .fg = VMEM_COLOR_WHITE,
        .bg = VMEM_COLOR_BLACK,
        .serial_out = 1
    };

    vmem_set_cursor(0, 0);
    vmem_clear_screen(&vmem_design);
    vmem_print(&vmem_design, "Welcome To AOS VM Safety Shell!\n\n");
    aos_shell_pm();
    for (;;) asm("hlt");
}

asm(".globl kernel_main");
