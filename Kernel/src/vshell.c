#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/pager.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

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
static aos_bool vshell_running = AOS_FALSE;
static char* prompt = DEF_PROMPT;

static int last_cmd = 0;

aos_bool is_ascii(char c) {
    return (c >= 0x20 && c <= 0x7E);
}

static aos_bool vshell_cmd_sysinfo(void) {
    aos_sysinfo_t* SystemInfo = kget_sysinfo();
	if (!SystemInfo) {
		if (!pyrion_builtin_print(vshell_ctx, "No Information Available....\n")) return AOS_FALSE;
		return AOS_TRUE;
	}

    if (!pyrion_builtin_print(vshell_ctx, "Boot Information:\n")) return AOS_FALSE;
    if (!pyrion_builtin_printf(vshell_ctx, "  Boot Drive: %d\n  Boot Mode: B=0x%x S=0x%x F=0x%x\n", SystemInfo->boot_drive.bus, SystemInfo->boot_drive.slot, SystemInfo->boot_drive.func, SystemInfo->boot_mode)) return AOS_FALSE;

    char cpu_vendor[14];
    memcpy(cpu_vendor, SystemInfo->cpu_vendor, 13);
    cpu_vendor[13] = '\0';

    if (!pyrion_builtin_print(vshell_ctx, "CPU Information:\n")) return AOS_FALSE;
    if (!pyrion_builtin_printf(vshell_ctx, "  CPU Signature: 0x%x\n  CPU Vendor: %s\n", SystemInfo->cpu_signature, cpu_vendor)) return AOS_FALSE;

    char* apic_present = SystemInfo->apic_present ? "True" : "False";

    if (!pyrion_builtin_print(vshell_ctx, "Additional Information:\n")) return AOS_FALSE;
    if (!pyrion_builtin_printf(vshell_ctx, "  APIC Present: %s\n  TSC Freq. : %llu Hz\n", apic_present, SystemInfo->tsc_freq_hz)) return AOS_FALSE;
    uint64_t ghz = SystemInfo->tsc_freq_hz / 1000000000;
    uint64_t mhz = (SystemInfo->tsc_freq_hz % 1000000000) / 1000000;
    if (!pyrion_builtin_printf(vshell_ctx, "  CPU Clock: %llu.%03llu GHz\n", ghz, mhz)) return AOS_FALSE;
	return AOS_TRUE;
}

static aos_bool vshell_handle_user_input(char* buf, int max_len, int* len) {
    int chars_typed = *len;

    char key = keyboard_ps2_try_get_char();
    if (key == 0) {
        return AOS_FALSE;
    }
    switch (key) {
        case '\b':
            if (chars_typed <= 0) break;
            if (!pyrion_builtin_printc(vshell_ctx, '\b')) return AOS_FALSE;
            chars_typed--;
            break;

        case '\n':
            if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
            *len = chars_typed;
            return AOS_TRUE;

        default:
            if (chars_typed == max_len || is_ascii(key) == 0) break;
            buf[chars_typed++] = key;
            if (!pyrion_builtin_printc(vshell_ctx, key)) return AOS_FALSE;
            break;
    }

    *len = chars_typed;
    return AOS_FALSE;
}

static aos_bool vshell_handle_shell(char* cmd_buf, int max_cmd_len, int* cmd_len) {
    if (!vshell_handle_user_input(cmd_buf, max_cmd_len - 1, cmd_len)) return AOS_TRUE;
    cmd_buf[max_cmd_len - 1] = '\0';

    if (*cmd_len == 0) return AOS_TRUE;

    cmd_buf[*cmd_len] = '\0';

    if (last_cmd == 1 || strcmp(cmd_buf, "exit") == 0) {
        if (last_cmd == 1) {
            if ((cmd_buf[0] == 'y' || cmd_buf[0] == 'Y') && *cmd_len == 1) {
                vshell_running = AOS_FALSE;
                asm("int $0x50");
            } else {
                if (!pyrion_builtin_print(vshell_ctx, "\nCancelled 'exit' Command!")) return AOS_FALSE;
            }
            if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
            last_cmd = 0;
        } else {
            if (!pyrion_builtin_print(vshell_ctx, "'Exit' Will not shutdown the machine but rather start AOS Safety Shell\nSure (y/N): ")) return AOS_FALSE;
            last_cmd = 1;
            *cmd_len = 0;
            return AOS_TRUE;
        }
    } else if (last_cmd == 2 || strcmp(cmd_buf, "reboot") == 0) {
        if (last_cmd == 2) {
            if ((cmd_buf[0] == 'y' || cmd_buf[0] == 'Y') && *cmd_len == 1) {
                pager_destroy_table(4);
                acpi_reboot();
                if (!pyrion_builtin_print(vshell_ctx, "\nFailed to reboot!")) return AOS_FALSE;
            } else {
                if (!pyrion_builtin_print(vshell_ctx, "\nCancelled 'reboot' Command!")) return AOS_FALSE;
            }
            if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
            last_cmd = 0;
        } else {
            if (!pyrion_builtin_print(vshell_ctx, "Sure (y/N): ")) return AOS_FALSE;
            last_cmd = 2;
            *cmd_len = 0;
            return AOS_TRUE;
        }
    } else if (last_cmd == 0) {
        if (strcmp(cmd_buf, "sysinfo") == 0) {
            if (!vshell_cmd_sysinfo()) return AOS_FALSE;
        } else if (strncmp(cmd_buf, "echo", 4) == 0) {
            if (max_cmd_len > 5) {
                if (!pyrion_builtin_print(vshell_ctx, (char*)(&(cmd_buf[5])))) return AOS_FALSE;
                if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
            }
        } else if (strcmp(cmd_buf, "clear") == 0) {
            if (!pyrion_clear(vshell_ctx, vshell_ctx->fb_info.bg_color)) return AOS_FALSE;
            if (!pyrion_set_cursor(vshell_ctx, 0, 0)) return AOS_FALSE;
        } else {
            if (!pyrion_builtin_print(vshell_ctx, "Unknown Command: ")) return AOS_FALSE;
            if (!pyrion_builtin_print(vshell_ctx, cmd_buf)) return AOS_FALSE;
            if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
        }
    } else {
        last_cmd = 0;
        if (!pyrion_builtin_print(vshell_ctx, "Unknown Command: ")) return AOS_FALSE;
        if (!pyrion_builtin_print(vshell_ctx, cmd_buf)) return AOS_FALSE;
        if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
    }

    // Reset
    *cmd_len = 0;
    if (!pyrion_builtin_print(vshell_ctx, prompt)) return AOS_FALSE;
	return AOS_TRUE;
}

void start_vshell(struct pyrion_ctx* display_ctx) {
    serial_print("Starting VShell...\n");
    gdisplay_ctx = display_ctx;
    vshell_ctx = pyrion_create_ctx();
    if (!vshell_ctx) return;

    vshell_ctx->cformat = PYRION_COLORF_RGBA;
    if (!pyrion_viewport(vshell_ctx, &vshell_viewport)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    if (!pyrion_conf(vshell_ctx, 0, 0, 0xFFFFFFFF, 0x171717FF)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    serial_print("[VSHELL] Pyrion Enabled, and set, clearing....\n");
    if (!pyrion_clear(vshell_ctx, 0x171717FF)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    serial_print("[VSHELL] Vshell initialized!\n");
    if (!pyrion_builtin_print(vshell_ctx, "Welcome to AOS++ Visible Shell!\n")) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    serial_print("[VSHELL] Vshell is active!\n");

    char cmd_buf[512];
    int cmd_len = 0;

    if (!pyrion_builtin_print(vshell_ctx, prompt)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}

    vshell_running = AOS_TRUE;
    while (vshell_running) { 
        if (!vshell_handle_shell((char*)cmd_buf, 512, &cmd_len)) break;
        if (!pyrion_flush(vshell_ctx)) break;
    }

    pyrion_destroy_ctx(vshell_ctx);
}
