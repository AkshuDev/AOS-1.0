#include <pbfs_blt_stub.h>

#include <system.h>

#include <inc/acpi.h>
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

// Define a static stack array
void kernel_main(void) __attribute__((used, noinline, section(".start"), noreturn));
void aos_shell_pm(void);
void exec_cmd(char* cmd, int lines, PM_Cursor_t* cur);
void cmd_start(char* program, int lines, PM_Cursor_t* cur);
void aospp_start();

void kernel_main(void) {
    // Now safe to use local variables
    PM_Cursor_t cur = {
        .x = 0,
        .y = 0,
        .fg = PM_COLOR_WHITE,
        .bg = PM_COLOR_BLACK
    };

    pm_set_cursor(&cur, 0, 0);
    pm_clear_screen(&cur);
    pm_print(&cur, "Welcome To AOS!\n\n");
    aos_shell_pm();
    for (;;) asm("hlt");
}

void aos_shell_pm(void) {
    PM_Cursor_t cur = {
        .x = 0,
        .y = 0,
        .fg = PM_COLOR_WHITE,
        .bg = PM_COLOR_BLACK
    };
    
    char input[SHELL_MAX_INPUT];
    int lines = 0;

    pm_set_cursor(&cur, 0, 0);
    pm_clear_screen(&cur);
    pm_print(&cur, "Welcome to AOS Shell!\n\n");
    lines += 2;
    // Read sysinfo
    uint8_t boot_drive = *AOS_BOOT_INFO_LOC;
    aos_sysinfo_t SystemInfo;
    PBFS_DP dp = {
        .count = AOS_SYSINFO_SPAN,
        .lba = AOS_SYSINFO_LBA
    };
    pm_read_sectors(&dp, &SystemInfo, boot_drive);
    pm_print(&cur, "SystemInfo:\n");
    pm_printf(&cur, "Boot Drive: 0x%x\nBoot Mode: 0x%x\n", SystemInfo.boot_drive, SystemInfo.boot_mode);
    pm_printf(&cur, "CPU Vendor: %s\n", (int)SystemInfo.cpu_vendor);
    pm_printf(&cur, "Total Memory: %u KiB\n\n", SystemInfo.total_memory_kib);
    lines += 8;

    while (1) {
        pm_print(&cur, "AOS: / $> "); // In AOS / means root and ~ means HOME_DIR like linux
        pm_read_line(input, SHELL_MAX_INPUT, &cur);

        exec_cmd(input, lines, &cur);
    }
}

void exec_cmd(char* cmd, int lines, PM_Cursor_t* cur) {
    if (str_eq(cmd, "help") == 1) {
        pm_print(cur, help_shell);
        lines += help_shell_nlines;
    } else if (str_n_eq(cmd, "echo ", 5) == 1) {
        pm_print(cur, cmd + 5);
        pm_print(cur, "\n");
        lines += 1;
    } else if (str_eq(cmd, "reboot") == 1) {
        pm_print(cur, "Rebooting...\n");
        acpi_reboot();
    } else if (str_eq(cmd, "clear") == 1) {
        pm_clear_screen(cur);
        lines = 0;
    } else if (str_n_eq(cmd, "start ", 6) == 1) {
        cmd_start(cmd + 6, lines, cur);
    } else {
        pm_print(cur, "Unknown command\n");
        lines += 1;
    }
    lines += 1;
    if (lines > VIDEO_HEIGHT) {
        pm_clear_screen(cur);
        lines = 0;
    }
}

void cmd_start(char* program, int lines, PM_Cursor_t* cur) {
    if (str_eq(program, "aospp") == 1 || str_eq(program, "aos++") == 1) {
        pm_print(cur, "Starting AOS++\n");
        lines += 1;
        aospp_start();
        pm_print(cur, "AOS++ Failed to load!\n");
        lines += 1;
    } else {
        pm_print(cur, "Unknown Program: ");
        pm_print(cur, program);
        pm_print(cur, "\n");
        lines += 1;
    }
}

asm(".globl kernel_main");
