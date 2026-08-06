#include <stdarg.h>

#include <aos_inttypes.h>
#include <asm.h>
#include <inc/core/pcie.h>
#include <inc/core/kfuncs.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/io/io.h>

extern uint8_t font8x16[256][16];

static struct gpu_device* gdevice = NULL;

static uint32_t* create_font_atlas_rgba(uint64_t* out_phys) {
    uint32_t atlas_w = 128; // 16 chars * 8 pixels
    uint32_t atlas_h = 256; // 16 rows * 16 pixels
    size_t size = atlas_w * atlas_h * sizeof(uint32_t);

    uint32_t* atlas = (uint32_t*)avmf_alloc(size, MALLOC_TYPE_DRIVER, AVMF_FLAG_RW | AVMF_FLAG_NO_CACHE, out_phys);
    if (!atlas) {
        serial_printf("[PYRION] Font Alloc failed!\n");
        return NULL;
    }
    memset(atlas, 0, size); // Clear with transparency

    for (int char_idx = 0; char_idx < 256; char_idx++) {
        // Calculate the character's position in the 16x16 grid
        int grid_x = (char_idx % 16) * 8;
        int grid_y = (char_idx / 16) * 16;

        for (int row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[char_idx][row];
            for (int col = 0; col < 8; col++) {
                int bit = (row_data >> (7 - col)) & 1;
                // Offset into the 128-wide buffer
                uint32_t pixel_idx = (grid_y + row) * atlas_w + (grid_x + col);
                atlas[pixel_idx] = bit ? 0xFFFFFFFF : 0x00000000;
            }
        }
    }
    return atlas;
}

static void pyrion_extract_rgba(enum pyrion_color_format cf, uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    uint8_t* c = (uint8_t*)&color;
    switch (cf) {
        case PYRION_COLORF_RGBA: {
            *r = c[0];
            *g = c[1];
            *b = c[2];
            *a = c[3];
            break;
        }
        case PYRION_COLORF_BGRA: {
            *r = c[2];
            *g = c[1];
            *b = c[0];
            *a = c[3];
            break;
        }
        case PYRION_COLORF_ABGR: {
            *r = c[3];
            *g = c[2];
            *b = c[1];
            *a = c[0];
            break;
        }
        case PYRION_COLORF_ARGB: {
            *r = c[1];
            *g = c[2];
            *b = c[3];
            *a = c[0];
            break;
        }
        case PYRION_COLORF_RGB: {
            *r = c[0];
            *g = c[1];
            *b = c[2];
            *a = 0xFF;
            break;
        }
        case PYRION_COLORF_BGR: {
            *r = c[2];
            *g = c[1];
            *b = c[0];
            *a = 0xFF;
            break;
        }
        default: {
            *r = 0xFF;
            *g = 0xFF;
            *b = 0xFF;
            *a = 0xFF;
            break;
        }
    }
}

aos_bool pyrion_init(struct gpu_device* device) {
    if (!device) return AOS_FALSE;
    gdevice = device;
    return gdevice->pyrion.init();
}

void pyrion_finish(void) {
    gdevice->pyrion.finish();
}

struct pyrion_ctx* pyrion_create_ctx(struct pyrion_create_ctx_info ctx_info) {
    struct pyrion_ctx* ctx = gdevice->pyrion.create_ctx(ctx_info);
    if (!ctx) return NULL;

    ctx->cformat = PYRION_COLORF_RGBA;
    ctx->display_info.width = gdevice->framebuffer->w;
    ctx->display_info.height = gdevice->framebuffer->h;
    ctx->display_info.bpp = gdevice->framebuffer->bpp;
    ctx->display_info.pitch = gdevice->framebuffer->pitch;
    ctx->display_info.size = gdevice->framebuffer->size;

    ctx->font.valid = AOS_FALSE;

    ctx->fb_cursor.bg_color = 0xFFFFFFFF;
    ctx->fb_cursor.fg_color = 0x000000FF;
    ctx->fb_cursor.x = 0;
    ctx->fb_cursor.y = 0;

    return ctx;
}

void pyrion_destroy_ctx(struct pyrion_ctx* ctx) {
    if (!ctx) return;

    if (ctx->font.valid) {
        if (ctx->font.atlas) avmf_free((uint64_t)ctx->font.atlas);
        ctx->font.valid = AOS_FALSE;
    }
    gdevice->pyrion.destroy_ctx(ctx);
}

void pyrion_unuse_device(struct pyrion_ctx* ctx) {
	if (!ctx) return;
	gdevice->pyrion.unuse_device(ctx);
}

aos_bool pyrion_enumerate_physical_devices(size_t* count, size_t idx, struct pyrion_physical_device* out) {
	if (!count && !out) return AOS_FALSE;
	return gdevice->pyrion.enumerate_physical_devices(count, idx, out);
}

aos_bool pyrion_use_device(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev) {
	if (!ctx || !dev) return AOS_FALSE;
	return gdevice->pyrion.use_device(ctx, dev);
}

aos_bool pyrion_viewport(struct pyrion_ctx* ctx, struct pyrion_rect viewport) {
    if (!ctx) return AOS_FALSE;
    return gdevice->pyrion.viewport(ctx, viewport);
}

aos_bool pyrion_submit_cmd_stream(struct pyrion_ctx* ctx, struct pyrion_cmd_stream* stream) {
	if (!ctx || !stream) return AOS_FALSE;
	return gdevice->pyrion.submit_cmd_stream(ctx, stream);
}

aos_bool pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (!ctx) return AOS_FALSE;
    ctx->fb_cursor.x = x;
    ctx->fb_cursor.y = y;
    ctx->fb_cursor.fg_color = fg;
    ctx->fb_cursor.bg_color = bg;
	return AOS_TRUE;
}

aos_bool pyrion_flush(struct pyrion_ctx* ctx) {
    if (!ctx) return AOS_FALSE;
    return gdevice->pyrion.flush(ctx);
}

aos_bool pyrion_clear(struct pyrion_ctx* ctx, uint32_t color) {
    if (!ctx) return AOS_FALSE;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, color, &r, &g, &b, &a);
    return gdevice->pyrion.clear(ctx, r, g, b, a);
}

aos_bool pyrion_pixel(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t color) {
    if (!ctx) return AOS_FALSE;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, color, &r, &g, &b, &a);
    return gdevice->pyrion.pixel(ctx, x, y, r, g, b, a);
}

aos_bool pyrion_blit(struct pyrion_ctx *ctx, uint32_t dst_res, uint32_t src_res, uint32_t width, uint32_t height) {
	if (!ctx) return AOS_FALSE;
	return gdevice->pyrion.blit(ctx, dst_res, src_res, width, height);
}

aos_bool pyrion_set_cursor(struct pyrion_ctx* ctx, uint32_t x, uint32_t y) {
    if (!ctx) return AOS_FALSE;
    ctx->fb_cursor.x = x;
    ctx->fb_cursor.y = y;
	return AOS_TRUE;
}

aos_bool pyrion_builtin_draw_rect(struct pyrion_ctx* ctx, struct pyrion_rect rect) {
    if (!ctx) return AOS_FALSE;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, rect.color, &r, &g, &b, &a);
    return gdevice->pyrion.draw_rect(ctx, rect.x, rect.y, rect.width, rect.height, r, g, b, a);
}

aos_bool pyrion_builtin_printc(struct pyrion_ctx* ctx, char c) {
    if (!ctx) return AOS_FALSE;

    if (!ctx->font.valid) {
		struct pyrion_font f = {
			.glyph_h = 16,
			.glyph_w = 8,
			.w = 128,
			.h = 256
		};
        f.atlas = create_font_atlas_rgba(&f.atlas_phys);
        if (!f.atlas || !f.atlas_phys) {
            serial_print("[PYRION] Failed to create font!\n");
            return AOS_FALSE;
        }
        serial_print("[PYRION] Uploading Font...\n");
        if (!gdevice->pyrion.upload_font(ctx, f, &ctx->font)) {
            serial_print("[PYRION] Failed to upload font!\n");
            return AOS_FALSE;
        }
        serial_print("[PYRION] Font uploaded!\n");
    }

	switch (c) {
		case '\n': {
			ctx->fb_cursor.x = 0;
			ctx->fb_cursor.y += ctx->font.glyph_h;
			return AOS_TRUE;
		}
		case ' ': {
			ctx->fb_cursor.x += ctx->font.glyph_w;
        	return AOS_TRUE;
		}
		case '\b': {

		}
		default: break;
	}
    
    uint8_t idx = (uint8_t)c;

	uint32_t columns = ctx->font.w / ctx->font.glyph_w;
    uint32_t atlas_x = (idx % columns) * ctx->font.glyph_w;
    uint32_t atlas_y = (idx / columns) * ctx->font.glyph_h;
    if (!gdevice->pyrion.draw_char(ctx, ctx->fb_cursor.x, ctx->fb_cursor.y, atlas_x, atlas_y, &ctx->font)) return AOS_FALSE;

    ctx->fb_cursor.x += ctx->font.glyph_w;

    if (ctx->fb_cursor.x + ctx->font.glyph_w > ctx->display_info.width) {
        ctx->fb_cursor.x = 0;
        ctx->fb_cursor.y += ctx->font.glyph_h;
    }
	return AOS_TRUE;
}

aos_bool pyrion_builtin_print(struct pyrion_ctx* ctx, const char* str) {
    if (!ctx) return AOS_FALSE;

    char* c = str;
    while (*c) {
        if (!pyrion_builtin_printc(ctx, *c)) return AOS_FALSE;
        c++;
    }
	return AOS_TRUE;
}

static aos_bool pyrion_builtin_print_ex_integer(struct pyrion_ctx* ctx, uint64_t val, int base, int width, int zero_pad, int is_signed) {
    char buf[64];
    const char* digits = "0123456789abcdef";
    int i = 0;
    int neg = 0;
    if (is_signed && (int64_t)val < 0) {
        neg = 1;
        val = -(int64_t)val;
    }
    do {
        buf[i++] = digits[val % base];
        val /= base;
    } while (val > 0);

    int total_len = i + (neg ? 1 : 0);
    if (width > total_len) {
        int padding_count = width - total_len;
        if (zero_pad) {
            if (neg) {
                if (!pyrion_builtin_printc(ctx, '-')) return AOS_FALSE;
                neg = 0;
            }
            while (padding_count--) {
				if (!pyrion_builtin_printc(ctx, '0')) return AOS_FALSE;
			}
        } else {
            while(padding_count--) {
				if (!pyrion_builtin_printc(ctx, ' ')) return AOS_FALSE;
			}
        }
    }

    if (neg) {
		if (!pyrion_builtin_printc(ctx, '-')) return AOS_FALSE;
	}
    while (i > 0) {
        if (!pyrion_builtin_printc(ctx, buf[--i])) return AOS_FALSE;
    }
	return AOS_TRUE;
}

aos_bool pyrion_builtin_printf(struct pyrion_ctx* ctx, const char* fmt, ...) {
    if (!ctx) return AOS_FALSE;

    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            int zero_pad = 0;
            int width = 0;
            int is_long = 0;

            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            while (*fmt == 'l') {
                is_long++;
                fmt++;
            }

            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    if (!pyrion_builtin_printc(ctx, c)) goto error_end;
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (!pyrion_builtin_print(ctx, s ? s : "(NULL)")) goto error_end;
                    break;
                }
                case 'S': {
					const char* s = va_arg(args, const char*);
					if (!s) {
						if (!pyrion_builtin_print(ctx, "(NULL)")) goto error_end;
					} else {
						for (size_t i = 0; i < width; i++) {
							char c = s[i];
							if (kc_is_printable(c)) {
								if (!pyrion_builtin_printc(ctx, c)) goto error_end;
							} else {
								if (!pyrion_builtin_printc(ctx, '\\')) goto error_end;
								if (!pyrion_builtin_printc(ctx, 'x')) goto error_end;
								if (!pyrion_builtin_print_ex_integer(ctx, (uint64_t)c, 16, 2, zero_pad, 0)) goto error_end;
							}
						}
					}
                    break;
				}
				case 'i':
                case 'd': { // signed 32/64-bit
                    int64_t d;
                    if (is_long >= 1) d = va_arg(args, int64_t);
                    else d = (int64_t)va_arg(args, int);
                    if (!pyrion_builtin_print_ex_integer(ctx, (uint64_t)d, 10, width, zero_pad, 1)) goto error_end;
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    if (!pyrion_builtin_print_ex_integer(ctx, u, 10, width, zero_pad, 0)) goto error_end;
                    break;
                }
                case 'x':
                case 'p': { // Pointer
                    uint64_t p;
                    if (*fmt == 'p') {
                        p = (uintptr_t)va_arg(args, void*);
                        if (width == 0) width = 16;
                        zero_pad = 1;
                    } else {
                        if (is_long >= 1) p = va_arg(args, uint64_t);
                        else p = (uint64_t)va_arg(args, uint32_t);
                    }
                    if (!pyrion_builtin_print_ex_integer(ctx, p, 16, width, zero_pad, 0)) goto error_end;
                    break;
                }
                case '%': {
                    if (!pyrion_builtin_printc(ctx, '%')) goto error_end;
                    break;
                }
                default: {
                    if (!pyrion_builtin_printc(ctx, *fmt)) goto error_end;
                    break;
                }
            }
        } else {
            if (!pyrion_builtin_printc(ctx, *fmt)) goto error_end;
        }
        fmt++;
    }

    va_end(args);
	return AOS_TRUE;

	error_end: {
		va_end(args);
		return AOS_FALSE;
	}
}

aos_bool pyrion_upload_font(struct pyrion_ctx* ctx, struct pyrion_font font, struct pyrion_font* out) {
	if (!ctx || !out) return AOS_FALSE;
	return gdevice->pyrion.upload_font(ctx, font, out);
}

void pyrion_destroy_font(struct pyrion_ctx* ctx, struct pyrion_font* font) {
	if (!ctx || !font) return;
	gdevice->pyrion.destroy_font(ctx, font);
}

aos_bool pyrion_switch_off(void) {
    return gdevice->switch_off(gdevice);
}
