#include <aos_inttypes.h>
#include <asm.h>
#include <system.h>
#include <uniboot.h>

#include <inc/mm/pager.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#include <PBFS/headers/pbfs.h>
#undef PBFS_NDRIVERS

#include <inc/mm/avmf.h>

#include <inc/core/kfuncs.h>
#include <inc/core/aos.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/keyboard/keyboard.h>
#include <inc/drivers/io/io.h>
#include <inc/core/acpi.h>
#include <inc/extra/aosbf.h>
#include <inc/core/vshell.h>

#define DEF_PROMPT " $ -> "

static struct pyrion_ctx* vshell_ctx = NULL;
static const struct pyrion_rect vshell_viewport = (const struct pyrion_rect){.x=0,.y=0,.width=800,.height=600,.color=0x121212FF};
static aos_bool vshell_running = AOS_FALSE;

static aos_bool need_reload_vshell = AOS_FALSE;

struct pbfs_mount* pbfs_mnt = NULL;
static char cwd[PBFS_MAX_PATH_LEN] = {'/', 0};
static int last_cmd = 0;

static aos_bool vshell_require_mounted(void) {
	if (!pbfs_mnt || !pbfs_mnt->active) {
		pyrion_builtin_print(vshell_ctx, "Error: No filesystem is mounted!\n");
		return AOS_FALSE;
	}

	return AOS_TRUE;
}

static aos_bool is_ascii(char c) {
    return (c >= 0x20 && c <= 0x7E);
}

static aos_bool vshell_cmd_sysinfo(void) {
    uniboot_boot_info* SystemInfo = kget_sysinfo();
	if (!SystemInfo) {
		if (!pyrion_builtin_print(vshell_ctx, "No Information Available....\n")) return AOS_FALSE;
		return AOS_TRUE;
	}

    if (!pyrion_builtin_print(vshell_ctx, "Boot Information:\n")) return AOS_FALSE;
    if (!pyrion_builtin_printf(vshell_ctx, "  Boot Drive: %d\n  Boot Mode: B=0x%x S=0x%x F=0x%x\n", SystemInfo->boot_drive.bus, SystemInfo->boot_drive.slot, SystemInfo->boot_drive.func, SystemInfo->boot_mode)) return AOS_FALSE;

    char cpu_vendor[33];
    memcpy(cpu_vendor, SystemInfo->cpu_info.vendor, 32);
    cpu_vendor[32] = '\0';

	char cpu_model[33];
    memcpy(cpu_model, SystemInfo->cpu_info.model, 32);
    cpu_model[32] = '\0';

    if (!pyrion_builtin_print(vshell_ctx, "CPU Information:\n")) return AOS_FALSE;
    if (!pyrion_builtin_printf(vshell_ctx, "  CPU Model: %s\n  CPU Vendor: %s\n", cpu_model, cpu_vendor)) return AOS_FALSE;
	
    if (!pyrion_builtin_print(vshell_ctx, "Additional Information:\n")) return AOS_FALSE;
    uint64_t ghz = SystemInfo->cpu_info.timer_freq / 1000000000;
    uint64_t mhz = (SystemInfo->cpu_info.timer_freq % 1000000000) / 1000000;
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
                __asm__("int $0x50");
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
        } else if (strncmp(cmd_buf, "echo ", 5) == 0 || strcmp(cmd_buf, "echo") == 0) {
            if (*cmd_len > 5) {
                if (!pyrion_builtin_print(vshell_ctx, (char*)(&(cmd_buf[5])))) return AOS_FALSE;
                if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
            } else {
				if (!pyrion_builtin_printc(vshell_ctx, '\n')) return AOS_FALSE;
			}
        } else if (strcmp(cmd_buf, "clear") == 0) {
            if (!pyrion_clear(vshell_ctx, vshell_ctx->fb_cursor.bg_color)) return AOS_FALSE;
            if (!pyrion_set_cursor(vshell_ctx, 0, 0)) return AOS_FALSE;
        } else if (strncmp(cmd_buf, "set-fsz", 7) == 0 || strcmp(cmd_buf, "set-fsz") == 0) {
			if (*cmd_len < 8) { // "set-fsz "
                if (!pyrion_builtin_print(vshell_ctx, "Usage: set-fsz <font size>\n")) return AOS_FALSE;
            } else {
				uint64_t size = kstr_to_u64(cmd_buf + 8, 10);
				if (size > 64) {
					if (!pyrion_builtin_print(vshell_ctx, "Error: Maximum supported size is '64'\n")) return AOS_FALSE;
				} else {
					pyrion_set_builtins_font_size(vshell_ctx, (uint32_t)(size & 0xFFFFFFFF));
					need_reload_vshell = AOS_TRUE;
				}
			}
		} else if (strcmp(cmd_buf, "ls") == 0 || strncmp(cmd_buf, "ls ", 3) == 0) {
			if (!vshell_require_mounted()) goto cmd_ls_end;

			char* arg = NULL;
			if (cmd_buf[2] == ' ') {
				arg = cmd_buf + 3;
			} else {
				arg = ".";
			}

			char path[PBFS_MAX_PATH_LEN];
			aos_create_path(path, cwd, arg);

			PBFS_DMM_Entry items[256];
			size_t item_count = 0;

			int out = pbfs_list_items(pbfs_mnt, path, items, 256, &item_count);

			if (out != PBFS_RES_SUCCESS) {
				if (!pyrion_builtin_printf(vshell_ctx, "Error: %s\n", pbfs_get_err_str(out))) return AOS_FALSE;
				goto cmd_ls_end;
			}

			if (!pyrion_builtin_printf(vshell_ctx, "Listing: %s\n", path)) return AOS_FALSE;

			for (size_t i = 0; i < item_count; i++) {
				char type = (items[i].type & METADATA_FLAG_DIR) ? 'd' : 'f';

				if (!pyrion_builtin_printf(
					vshell_ctx,
					"%c %s (lba %llu)\n",
					type,
					items[i].name,
					uint128_to_u64(items[i].lba)
				)) return AOS_FALSE;
			}

			cmd_ls_end: {}
		} else if (strcmp(cmd_buf, "mkdir") == 0 || strncmp(cmd_buf, "mkdir ", 6) == 0) {
			if (cmd_len < 7) {
				if (!pyrion_builtin_print(vshell_ctx, "Usage: mkdir <directory>\n")) return AOS_FALSE;
				goto cmd_mkdir_end;
			}
			if (!vshell_require_mounted()) goto cmd_mkdir_end;

			PBFS_DMM_Entry entry;
			uint64_t lba = 0;

			char path[PBFS_MAX_PATH_LEN];
			aos_create_path(path, cwd, cmd_buf + 6);

			int out = pbfs_find_entry(path, &entry, &lba, pbfs_mnt);
			if (out == PBFS_RES_SUCCESS || out == -1) {
				if (!pyrion_builtin_print(vshell_ctx, "Error: File or Directory already exists!\n")) return AOS_FALSE;
				goto cmd_mkdir_end;
			}

			out = pbfs_add_dir(pbfs_mnt, path, 0, 0, (PBFS_Permission_Flags)(PERM_READ | PERM_WRITE));
			if (out != PBFS_RES_SUCCESS) {
				if (!pyrion_builtin_printf(vshell_ctx, "Error: %s\n", pbfs_get_err_str(out))) return AOS_FALSE;
				goto cmd_mkdir_end;
			}

			cmd_mkdir_end: {}
		} else if (strcmp(cmd_buf, "cd") == 0 || strncmp(cmd_buf, "cd ", 3) == 0) {
			if (cmd_len < 4) {
				if (!pyrion_builtin_print(vshell_ctx, "Usage: cd <directory>\n")) return AOS_FALSE;
				goto cmd_cd_end;
			}
			if (!vshell_require_mounted()) goto cmd_cd_end;

			PBFS_DMM_Entry entry;
			uint64_t lba = 0;
			
			char path[PBFS_MAX_PATH_LEN];
			aos_create_path(path, cwd, cmd_buf + 3);

			int out = pbfs_find_entry(path, &entry, &lba, pbfs_mnt);
			if (out != PBFS_RES_SUCCESS || !(entry.type & METADATA_FLAG_DIR)) {
				if (!pyrion_builtin_print(vshell_ctx, "Error: No such directory!\n")) return AOS_FALSE;
				goto cmd_cd_end;
			}

			strncpy(cwd, path, PBFS_MAX_PATH_LEN - 1);
			cwd[PBFS_MAX_PATH_LEN - 1] = '\0';

			cmd_cd_end: {}
		} else if (strcmp(cmd_buf, "set-font") == 0 || strncmp(cmd_buf, "set-font ", 9) == 0) {
			if (cmd_len < 10) {
				if (!pyrion_builtin_print(vshell_ctx, "Usage: set-font <path to aosbf>\n")) return AOS_FALSE;
				goto cmd_set_font_end;
			}

			char* font_path = cmd_buf + 9;
			struct pyrion_font font = {0};

			if (!aosbf_file_to_pyrion_font(font_path, &font)) {
				if (!pyrion_builtin_print(vshell_ctx, "Error: Could not load font.\n")) return AOS_FALSE;
				goto cmd_set_font_end;
			}

			if (!pyrion_set_builtins_font(vshell_ctx, &font)) {
				if (!pyrion_builtin_print(vshell_ctx, "Error: Could not set font.\n")) return AOS_FALSE;

				if (font.atlas) avmf_free((uint64_t)font.atlas);
				if (font.glyphs && font.glyph_count) avmf_free((uint64_t)font.glyphs);

				goto cmd_set_font_end;
			}

			if (!pyrion_builtin_print(vshell_ctx, "Font changed.\n")) return AOS_FALSE;
			need_reload_vshell = AOS_TRUE;

			cmd_set_font_end: {}
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
	if (!pyrion_builtin_print(vshell_ctx, cwd)) return AOS_FALSE;
	if (!pyrion_builtin_print(vshell_ctx, DEF_PROMPT)) return AOS_FALSE;
	return AOS_TRUE;
}

static aos_bool vshell_enum_n_set_best_dev(struct pyrion_ctx* ctx) {
	if (!ctx) return AOS_FALSE;

	uint64_t count = 0;
	if (!pyrion_enumerate_physical_devices(&count, 0, NULL)) return AOS_FALSE;
	if (count < 1) return AOS_FALSE;

	struct pyrion_physical_device best_device = {0};
	aos_bool found_best = AOS_FALSE;

	serial_printf("[VSHELL] Enumerating Pyrion GPU Devices (Count - %llu)\n", count);

	for (uint64_t i = 0; i < count; i++) {
		struct pyrion_physical_device pdev = {0};
		if (!pyrion_enumerate_physical_devices(NULL, i, &pdev)) continue;

		serial_printf("[VSHELL] #%llu Device - %s\n", i, pdev.name);
		serial_printf("\t[VSHELL] Acceleration: %s\n", pdev.accelerated ? "True" : "False");
		serial_printf("\t[VSHELL] CMD Core Present: %s (Core - %llu)\n", pdev.cmd_core != 0xFFFF ? "True" : "False", pdev.cmd_core);

		if (!found_best) {
			best_device = pdev;
			found_best = AOS_TRUE;
			continue;
		}

		// P1 - Acceleration
		if (pdev.accelerated && !best_device.accelerated) { best_device = pdev; found_best = AOS_TRUE; continue; }
		// P2 - Cmd Core
		if (pdev.cmd_core != 0xFFFF && best_device.cmd_core == 0xFFFF) { best_device = pdev; found_best = AOS_TRUE; continue; }
	}
	if (!found_best) return AOS_FALSE;

	serial_printf("[VSHELL] Using (#%llu) Device - %s\n", best_device.idx, best_device.name);

	return pyrion_use_device(ctx, &best_device);
} 

void start_vshell(void) {
    serial_print("[VSHELL] Starting VShell...\n[VSHELL] Creating Pyrion Context...\n");
	struct pyrion_create_ctx_info pcreate_info = {
		.name = "VShell"
	};
    vshell_ctx = pyrion_create_ctx(pcreate_info);
	if (!vshell_ctx) return;

	pbfs_mnt = aos_get_mounted_fs();
	memset(cwd, 0, sizeof(cwd));
	cwd[0] = '/';

	serial_print("[VSHELL] Selecting GPU....\n");
	if (!vshell_enum_n_set_best_dev(vshell_ctx)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}

    vshell_ctx->cformat = PYRION_COLORF_RGBA;
    serial_print("[VSHELL] Setting Pyrion Viewport...\n");
	if (!pyrion_viewport(vshell_ctx, vshell_viewport)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    
	serial_print("[VSHELL] Initializing Pyrion Configuration...\n");
	if (!pyrion_conf(vshell_ctx, 0, 0, 0xFFFFFFFF, 0x171717FF)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
	
	serial_print("[VSHELL] Initializing Pyrion Default Font...\n");
	if (!pyrion_set_default_builtins_font(vshell_ctx)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
	
	serial_print("[VSHELL] Setting Pyrion Font Size...\n");
	if (!pyrion_set_builtins_font_size(vshell_ctx, 16.0f)) {
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

	memset(cmd_buf, 0, sizeof(cmd_buf));
	last_cmd = 0;

	if (!pyrion_builtin_print(vshell_ctx, cwd)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}
    if (!pyrion_builtin_print(vshell_ctx, DEF_PROMPT)) {
		pyrion_destroy_ctx(vshell_ctx);
		return;
	}

    vshell_running = AOS_TRUE;
	need_reload_vshell = AOS_FALSE;

    while (vshell_running) {
		if (need_reload_vshell) {
			memset(cmd_buf, 0, sizeof(cmd_buf));
			if (!pyrion_clear(vshell_ctx, vshell_ctx->fb_cursor.bg_color)) break;
            if (!pyrion_set_cursor(vshell_ctx, 0, 0)) break;
			if (!pyrion_builtin_print(vshell_ctx, cwd)) break;
			if (!pyrion_builtin_print(vshell_ctx, DEF_PROMPT)) break;

			if (!pyrion_flush(vshell_ctx)) break;

			need_reload_vshell = AOS_FALSE;
			continue;
		}

        if (!vshell_handle_shell((char*)cmd_buf, sizeof(cmd_buf), &cmd_len)) break;
        if (!pyrion_flush(vshell_ctx)) break;
    }

    pyrion_destroy_ctx(vshell_ctx);
}
