#include <pbfs_blt_stub.h>
#define SHELL_MAX_INPUT 128

static char* help_shell = "Usage: [COMMAND]\n"
    "Commands:\n"
    "\thelp: Provides this output.\n"
    "\techo [msg]: Prints [msg] on stdout\n"
    "\treboot: Reboots the system\n";

// Define a static stack array
void kernel_main(void) __attribute__((used, noinline, section(".start"), noreturn));
void aos_shell_pm(void);

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

    pm_set_cursor(&cur, 0, 0);
    pm_clear_screen(&cur);
    pm_print(&cur, "Welcome to AOS Shell!\n\n\n");

    while (1) {
        pm_print(&cur, "AOS: / $> "); // In AOS / means root and ~ means HOME_DIR like linux
        pm_read_line(input, SHELL_MAX_INPUT, &cur);

        if (str_eq(input, "help") == 0) {
            pm_print(&cur, help_shell);
        } else if (str_n_eq(input, "echo ", 5) == 0) {
            pm_print(&cur, input + 5);
            pm_print(&cur, "\n");
        } else if (str_eq(input, "reboot") == 0) {
            pm_print(&cur, "Rebooting...\n");
            asm("hlt"); // TODO: Replace with reboot logic
        } else {
            pm_print(&cur, "Unknown command\n");
        }
    }
}

asm(".globl kernel_main");
