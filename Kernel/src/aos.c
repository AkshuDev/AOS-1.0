#include <system.h>
#include <inttypes.h>

#include <inc/kfuncs.h>
#include <inc/acpi.h>
#include <inc/idt.h>
#include <inc/io.h>
#include <inc/pcie.h>

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
    "\tstart <program>: Starts a Program, Use 'start -help' for more info\n"
    "\tmemdump <addr> <size>: Prints memory at the specified address for the specified size\n";

static int help_shell_nlines = 8;

// Define a static stack array
void kernel_main(void) __attribute__((used, noinline, section(".start"), noreturn));
void aos_shell_pm(void);
void exec_cmd(char* cmd, int* lines, struct VMemDesign* vmem_design);
static void cmd_print_help(struct VMemDesign* vmem_design, int* lines);
void cmd_start(char* program, int* lines, struct VMemDesign* vmem_design);
void aospp_start();

void kernel_main(void) {
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
    int lines = 0;

    vmem_set_cursor(0, 0);
    vmem_clear_screen(&vmem_design);
    vmem_print(&vmem_design, "Welcome to AOS Shell!\n\n");
    lines += 2;
    // Read sysinfo
    uint8_t boot_drive = *AOS_BOOT_INFO_LOC;
    aos_sysinfo_t SystemInfo;
    struct ATA_DP dp = {
        .count = AOS_SYSINFO_SPAN,
        .lba = AOS_SYSINFO_LBA
    };
    ata_read_sectors(&dp, &SystemInfo, boot_drive);
    vmem_print(&vmem_design, "SystemInfo:\n");
    vmem_printf(&vmem_design, "Boot Drive: 0x%llx (%llu)\nBoot Mode: 0x%llx (%llu)\n", (uint64_t)SystemInfo.boot_drive, (uint64_t)SystemInfo.boot_drive, (uint64_t)SystemInfo.boot_mode, (uint64_t)SystemInfo.boot_mode);
    vmem_printf(&vmem_design, "CPU Vendor: %s\n", (uintptr_t)&SystemInfo.cpu_vendor);
    vmem_printf(&vmem_design, "Total Memory: %llu KiB\n\n", (uint64_t)SystemInfo.total_memory_kib);
    lines += 8;

    while (1) {
        vmem_print(&vmem_design, "AOS: / $> "); // In AOS / means root and ~ means HOME_DIR like linux
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
        acpi_reboot();
    } else if (strcmp(cmd, "clear") == 0) {
        vmem_clear_screen(vmem_design);
        *lines = 0;
    } else if (strncmp(cmd, "start ", 6) == 0) {
        cmd_start(cmd + 6, lines, vmem_design);
    } else if (strncmp(cmd, "memdump ", 8) == 1) {
        uintptr_t addr = str_to_uint(cmd + 8);
        for (int i = 0; i < 16; i++) {
            vmem_printf(vmem_design, "0x%08x: %02x %02x %02x %02x\n", 
                    addr + i*4,
                    *((uint8_t*)(addr + i*4)),
                    *((uint8_t*)(addr + i*4 +1)),
                    *((uint8_t*)(addr + i*4 +2)),
                    *((uint8_t*)(addr + i*4 +3)));
            *lines++;
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

asm(".globl kernel_main");
