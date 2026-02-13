#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/pager.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/core/vshell.h>
#include <inc/drivers/keyboard/keyboard.h>
#include <inc/drivers/io/io.h>
#include <inc/core/acpi.h>

#define DEF_PROMPT "/ $ -> "

static struct pyrion_ctx* vshell_ctx = NULL;
static struct pyrion_rect vshell_viewport = (struct pyrion_rect){.x=0,.y=0,.width=800,.height=600,.color=0x121212FF};
static struct pyrion_ctx* gdisplay_ctx = NULL;
static uint8_t vshell_running = 0;
static char* prompt = DEF_PROMPT;

static int last_cmd = 0;

static uint8_t is_ascii(char c) {
    return (c >= 0x20 && c <= 0x7E);
}

static void vshell_cmd_sysinfo(void) {
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;

    pyrion_builtin_print(vshell_ctx, "Boot Information:\n");
    pyrion_builtin_printf(vshell_ctx, "  Boot Drive: %d\n  Boot Mode: %d\n", SystemInfo->boot_drive, SystemInfo->boot_mode);

    char cpu_vendor[14];
    memcpy(cpu_vendor, SystemInfo->cpu_vendor, 13);
    cpu_vendor[13] = '\0';

    pyrion_builtin_print(vshell_ctx, "CPU Information:\n");
    pyrion_builtin_printf(vshell_ctx, "  CPU Signature: 0x%x\n  CPU Vendor: %s\n", SystemInfo->cpu_signature, cpu_vendor);

    char* apic_present = SystemInfo->apic_present ? "True" : "False";

    pyrion_builtin_print(vshell_ctx, "Additional Information:\n");
    pyrion_builtin_printf(vshell_ctx, "  APIC Present: %s\n  TSC Freq. : %d Hz\n", apic_present, SystemInfo->tsc_freq_hz);
}

static uint8_t vshell_handle_user_input(char* buf, int max_len, int* len) {
    int chars_typed = *len;

    char key = keyboard_ps2_try_get_char();
    if (key == 0) {
        return 0;
    }
    switch (key) {
        case '\b':
            if (chars_typed <= 0) break;
            pyrion_builtin_printc(vshell_ctx, '\b');
            chars_typed--;
            break;

        case '\n':
            pyrion_builtin_printc(vshell_ctx, '\n');
            *len = chars_typed;
            return 1;

        default:
            if (chars_typed == max_len || is_ascii(key) == 0) break;
            buf[chars_typed++] = key;
            pyrion_builtin_printc(vshell_ctx, key);
            break;
    }

    *len = chars_typed;
    return 0;
}

static void vshell_handle_shell(char* cmd_buf, int max_cmd_len, int* cmd_len) {
    if (vshell_handle_user_input(cmd_buf, max_cmd_len - 1, cmd_len) == 0) return;
    cmd_buf[max_cmd_len - 1] = '\0';

    if (*cmd_len == 0) return;

    cmd_buf[*cmd_len] = '\0';

    if (last_cmd == 1 || strcmp(cmd_buf, "exit") == 0) {
        if (last_cmd == 1) {
            if ((cmd_buf[0] == 'y' || cmd_buf[0] == 'Y') && *cmd_len == 1)
                vshell_running = 0;
            else
                pyrion_builtin_print(vshell_ctx, "\nCancelled 'exit' Command!");
            pyrion_builtin_printc(vshell_ctx, '\n');
            last_cmd = 0;
        } else {
            pyrion_builtin_print(vshell_ctx, "'Exit' Will not shutdown the machine but rather return to the normal shell\n NOTE: The shell will work however the display will be hanged\n  Sure (y/N): ");
            last_cmd = 1;
            *cmd_len = 0;
            return;
        }
    } else if (last_cmd == 2 || strcmp(cmd_buf, "reboot") == 0) {
        if (last_cmd == 2) {
            if ((cmd_buf[0] == 'y' || cmd_buf[0] == 'Y') && *cmd_len == 1) {
                pager_destroy_table(4);
                acpi_reboot();
                pyrion_builtin_print(vshell_ctx, "\nFailed to reboot!");
            } else {
                pyrion_builtin_print(vshell_ctx, "\nCancelled 'reboot' Command!");
            }
            pyrion_builtin_printc(vshell_ctx, '\n');
            last_cmd = 0;
        } else {
            pyrion_builtin_print(vshell_ctx, "Sure (y/N): ");
            last_cmd = 2;
            *cmd_len = 0;
            return;
        }
    } else if (last_cmd == 0) {
        if (strcmp(cmd_buf, "sysinfo") == 0) {
            vshell_cmd_sysinfo();
        } else if (strncmp(cmd_buf, "echo", 4) == 0) {
            if (max_cmd_len > 5) {
                pyrion_builtin_print(vshell_ctx, (char*)(&(cmd_buf[5])));
                pyrion_builtin_printc(vshell_ctx, '\n');
            }
        } else if (strcmp(cmd_buf, "clear") == 0) {
            pyrion_clear(vshell_ctx, vshell_ctx->fb_cursor.bg_color);
            pyrion_set_cursor(vshell_ctx, 0, 0);
        } else {
            pyrion_builtin_print(vshell_ctx, "Unknown Command: ");
            pyrion_builtin_print(vshell_ctx, cmd_buf);
            pyrion_builtin_printc(vshell_ctx, '\n');
        }
    } else {
        last_cmd = 0;
        pyrion_builtin_print(vshell_ctx, "Unknown Command: ");
        pyrion_builtin_print(vshell_ctx, cmd_buf);
        pyrion_builtin_printc(vshell_ctx, '\n');
    }

    // Reset
    *cmd_len = 0;
    pyrion_builtin_print(vshell_ctx, prompt);
}

void start_vshell(struct pyrion_ctx* display_ctx) {
    serial_print("Starting VShell...\n");
    gdisplay_ctx = display_ctx;
    vshell_ctx = pyrion_create_ctx();
    if (!vshell_ctx) return;

    pyrion_viewport(vshell_ctx, &vshell_viewport);
    pyrion_conf(vshell_ctx, 0, 0, 0xFFFFFFFF, 0x171717FF);
    pyrion_clear(vshell_ctx, 0x171717FF);
    pyrion_builtin_print(vshell_ctx, "Welcome to AOS++ Visible Shell!\n");

    char cmd_buf[512];
    int cmd_len = 0;

    pyrion_builtin_print(vshell_ctx, prompt);

    vshell_running = 1;
    while (vshell_running) { 
        vshell_handle_shell((char*)cmd_buf, 512, &cmd_len);
        pyrion_flush(vshell_ctx);
    }

    pyrion_destroy_ctx(vshell_ctx);
}
