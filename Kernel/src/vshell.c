#include <inttypes.h>
#include <asm.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/core/vshell.h>
#include <inc/drivers/keyboard/keyboard.h>
#include <inc/drivers/io/io.h>

#define DEF_PROMPT "/ $ -> "

static struct pyrion_ctx* vshell_ctx = NULL;
static struct pyrion_rect vshell_viewport = (struct pyrion_rect){.x=0,.y=0,.width=500,.height=300,.color=0x121212FF};
static struct pyrion_ctx* gdisplay_ctx = NULL;
static uint8_t vshell_running = 0;
static char* prompt = DEF_PROMPT;

static int last_cmd = 0;

static uint8_t is_ascii(char c) {
    return (c >= 0x20 && c <= 0x7E);
}

static uint8_t vshell_handle_user_input(char* buf, int max_len, int* len) {
    int chars_typed = *len;

    char key = keyboard_ps2_get_char();
    switch (key) {
        case '\b':
            pyrion_builtin_printc(vshell_ctx, '\b');
            if (chars_typed > 0) chars_typed--;
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
    } else {
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
    pyrion_conf(vshell_ctx, 0, 0, 0xFFFFFFFF, 0x000000FF);
    pyrion_builtin_print(vshell_ctx, "Welcome to AOS++ Visible Shell!\n");

    char cmd_buf[512];
    int cmd_len = 0;

    pyrion_builtin_print(vshell_ctx, prompt);

    vshell_running = 1;
    while (vshell_running) { 
        vshell_handle_shell((char*)cmd_buf, 512, &cmd_len);
        pyrion_flush(vshell_ctx);
    }
}
