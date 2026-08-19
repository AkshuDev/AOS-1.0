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

static aos_bool create_default_builtin_font(struct pyrion_font* out) {
	if (!out) return AOS_FALSE;
	memset(out, 0, sizeof(*out));

	uint32_t atlas_w = 128; // 16 chars * 8 pixels
    uint32_t atlas_h = 256; // 16 rows * 16 pixels
    size_t size = atlas_w * atlas_h * sizeof(uint32_t);

	out->w = atlas_w;
	out->h = atlas_h;
	out->line_height = 16;
	out->line_gap = 0;
	out->font_size = 16;
	out->base_font_size = 16;

    uint32_t* atlas = (uint32_t*)avmf_alloc(size, MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, &out->atlas_phys);
    if (!atlas) {
        serial_printf("[PYRION] Font Atlas Allocation failed!\n");
        return AOS_FALSE;
    }
    memset(atlas, 0, size); // Clear with transparency

	uint32_t glyph_count = 0;
	for (uint32_t codepoint = 0; codepoint <= 0xFF; codepoint++) {
		if (!kc_is_printable((char)codepoint)) continue;
		glyph_count++;
	}

	struct pyrion_glyph* glyphs = (struct pyrion_glyph*)avmf_alloc(sizeof(struct pyrion_glyph) * glyph_count, MALLOC_TYPE_DRIVER, AVMF_FLAG_RW, NULL);
    if (!glyphs) {
        serial_printf("[PYRION] Font Glyph list allocation failed!\n");
		avmf_free((uint64_t)atlas);
        return AOS_FALSE;
    }
	memset(glyphs, 0, sizeof(struct pyrion_glyph) * glyph_count);

	out->atlas = (uint8_t*)atlas;
	out->glyphs = glyphs;
	out->glyph_count = glyph_count;

	uint32_t gidx = 0;
    for (uint32_t codepoint = 0; codepoint <= 0xFF; codepoint++) {
		if (!kc_is_printable((char)codepoint)) continue;

		struct pyrion_glyph* g = &glyphs[gidx++];
		g->codepoint = codepoint;
        g->width = 8;
        g->height = 16;
        g->bearing_x = 0;
        g->bearing_y = 16;
        g->advance_x = 8;
        g->advance_y = 0;

        // Calculate the character's position in the 16x16 grid
        uint32_t atlas_x = (codepoint % 16) * 8;
        uint32_t atlas_y = (codepoint / 16) * 16;

		g->atlas_x = atlas_x;
        g->atlas_y = atlas_y;

        for (uint32_t row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[codepoint][row];
            for (uint32_t col = 0; col < 8; col++) {
                uint32_t bit = (row_data >> (7 - col)) & 1;
                // Offset into the 128-wide buffer
                uint32_t pixel_idx = (atlas_y + row) * atlas_w + (atlas_x + col);
                atlas[pixel_idx] = bit ? 0xFFFFFFFF : 0x00000000;
            }
        }

		g->valid = AOS_TRUE;

		if (codepoint == '?') out->fallback_glyph = *g;
    }

    out->valid = AOS_TRUE;
    return AOS_TRUE;
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
        pyrion_destroy_font(ctx, &ctx->font);
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
		if (!pyrion_set_default_builtins_font(ctx)) return AOS_FALSE;
    }

	uint32_t scale = ctx->font.font_size / ctx->font.base_font_size;
	if (scale == 0) scale = 1;

	uint32_t line_height = ctx->font.line_height * scale;
	uint32_t line_gap = ctx->font.line_gap * scale;

	switch (c) {
		case '\n': {
			uint32_t advance_value = line_height + line_gap;

			if (ctx->fb_cursor.y + advance_value <= ctx->viewport.height) {
				ctx->fb_cursor.x = 0;
				ctx->fb_cursor.y += advance_value;
				return AOS_TRUE;
			}
			return AOS_FALSE;
		}
		case '\r': {
            ctx->fb_cursor.x = 0;
            return AOS_TRUE;
		}
		default: break;
	}

	struct pyrion_glyph* g_rendered = NULL;
    if (!gdevice->pyrion.draw_char(ctx, ctx->fb_cursor.x + ctx->viewport.x, ctx->fb_cursor.y + ctx->viewport.y, (uint32_t)c, &ctx->font, &g_rendered)) return AOS_FALSE;

	if (!g_rendered) return AOS_FALSE;
	if (!g_rendered->valid) return AOS_FALSE;

	uint32_t advance_x = g_rendered->advance_x * scale;

	if (ctx->fb_cursor.x + advance_x > ctx->viewport.width) {
		if (ctx->fb_cursor.y + line_height + line_gap > ctx->viewport.height) {
			return AOS_FALSE;
		} else {
			ctx->fb_cursor.x = 0;
			ctx->fb_cursor.y += line_height + line_gap;
		}
	} else {
		ctx->fb_cursor.x += advance_x;
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

static aos_bool pyrion_builtin_print_ex_string(struct pyrion_ctx* ctx, const char* str, int width, int zero_pad) {
	int len = 0;
	while (str[len])
		len++;

	int neg = (str[0] == '-');
	if (width > len) {
		int padding = width - len;

		if (zero_pad && neg) {
			if (!pyrion_builtin_printc(ctx, '-')) return AOS_FALSE;
			str++;

			while (padding--)
				if (!pyrion_builtin_printc(ctx, '0')) return AOS_FALSE;
		} else {
			char pad = zero_pad ? '0' : ' ';
			while (padding--)
				if (!pyrion_builtin_printc(ctx, pad)) return AOS_FALSE;
		}
	}

	while (*str)
		if (!pyrion_builtin_printc(ctx, *str++)) return AOS_FALSE;
	return AOS_TRUE;
}

static aos_bool pyrion_builtin_print_ex_integer(struct pyrion_ctx* ctx, uint64_t val, int base, int width, int zero_pad, aos_bool is_signed, aos_bool caps) {
	char buf[64];
	char* str;

	if (is_signed) str = ki64_to_str((int64_t)val, buf, sizeof(buf), base, caps);
	else str = ku64_to_str(val, buf, sizeof(buf), base, caps);

	if (!str) return AOS_FALSE;

	return pyrion_builtin_print_ex_string(ctx, str, width, zero_pad);
}

static aos_bool pyrion_builtin_print_ex_float(struct pyrion_ctx* ctx, double val, int width, int precision, int zero_pad) {
	char buf[128];
	if (!kdouble_to_str(val, buf, sizeof(buf), precision)) return AOS_FALSE;

	return pyrion_builtin_print_ex_string(ctx, buf, width, zero_pad);
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
			int precision = 0;

            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
			if (*fmt == '.') {
				fmt++;
				precision = 0;
				while (*fmt >= '0' && *fmt <= '9') {
					precision = precision * 10 + (*fmt - '0');
					fmt++;
				}
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
								if (!pyrion_builtin_print_ex_integer(ctx, (uint64_t)c, 16, 2, zero_pad, AOS_FALSE, AOS_FALSE)) goto error_end;
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
                    if (!pyrion_builtin_print_ex_integer(ctx, (uint64_t)d, 10, width, zero_pad, AOS_TRUE, AOS_FALSE)) goto error_end;
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    if (!pyrion_builtin_print_ex_integer(ctx, u, 10, width, zero_pad, AOS_FALSE, AOS_FALSE)) goto error_end;
                    break;
                }
                case 'x': {
					uint64_t p;

					if (is_long >= 1) p = va_arg(args, uint64_t);
                	else p = (uint64_t)va_arg(args, uint32_t);
                    
                    if (!pyrion_builtin_print_ex_integer(ctx, p, 16, width, zero_pad, AOS_FALSE, AOS_FALSE)) goto error_end;
                    break;
				}
				case 'X':
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
                    if (!pyrion_builtin_print_ex_integer(ctx, p, 16, width, zero_pad, AOS_FALSE, AOS_TRUE)) goto error_end;
                    break;
                }
                case 'f': { // Pointer
                    double f;
					if (is_long >= 1) f = va_arg(args, double);
					else f = (double)va_arg(args, float);

                    if (!pyrion_builtin_print_ex_float(ctx, f, width, precision, zero_pad)) goto error_end;
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

aos_bool pyrion_set_builtins_font(struct pyrion_ctx* ctx, struct pyrion_font* font) {
	if (!ctx || !font) return AOS_FALSE;
	
	if (
		!ctx->valid ||
        !font->valid ||
        !font->atlas ||
        !font->atlas_phys ||
        !font->glyphs ||
        font->glyph_count == 0 ||
        font->w == 0 ||
        font->h == 0 ||
        font->line_height == 0
	) {

        return AOS_FALSE;
    }

	if (ctx->font.valid) {
		pyrion_destroy_font(ctx, &ctx->font);
	}
	ctx->font = *font;
	return AOS_TRUE;
}

aos_bool pyrion_set_builtins_font_size(struct pyrion_ctx* ctx, uint32_t size) {
	if (!ctx || size < 1) return AOS_FALSE;
	return pyrion_set_font_size(ctx, &ctx->font, size);
}

aos_bool pyrion_set_default_builtins_font(struct pyrion_ctx* ctx) {
	if (!ctx) return AOS_FALSE;
	if (!ctx->valid) return AOS_FALSE;

	if (ctx->font.valid) {
		pyrion_destroy_font(ctx, &ctx->font);
	}
	
	struct pyrion_font f = {0};
	if (!create_default_builtin_font(&f)) {
		serial_print("[PYRION] Failed to create font!\n");
		return AOS_FALSE;
	}

	serial_print("[PYRION] Uploading Font...\n");
	f.valid = AOS_FALSE;
	if (!gdevice->pyrion.upload_font(ctx, f, &ctx->font)) {
		if (f.atlas) avmf_free((uint64_t)f.atlas);
		if (f.glyphs && f.glyph_count > 0) avmf_free((uint64_t)f.glyphs);

		serial_print("[PYRION] Failed to upload font!\n");
		return AOS_FALSE;
	}
	serial_print("[PYRION] Font uploaded!\n");

	return AOS_TRUE;
}

aos_bool pyrion_upload_font(struct pyrion_ctx* ctx, struct pyrion_font font, struct pyrion_font* out) {
	if (!ctx || !out) return AOS_FALSE;
	return gdevice->pyrion.upload_font(ctx, font, out);
}

void pyrion_destroy_font(struct pyrion_ctx* ctx, struct pyrion_font* font) {
	if (!ctx || !font) return;
	gdevice->pyrion.destroy_font(ctx, font);

	if (font->atlas) {
		avmf_free((uint64_t)font->atlas);
		font->atlas = NULL;
		font->atlas_phys = 0;
	}

	if (font->glyphs && font->glyph_count > 0) {
		avmf_free((uint64_t)font->glyphs);
		font->glyphs = NULL;
		font->glyph_count = 0;
	}

	font->valid = AOS_FALSE;
}

aos_bool pyrion_set_font_size(struct pyrion_ctx* ctx, struct pyrion_font* font, uint32_t size) {
	if (!ctx || !font || size < 1) return AOS_FALSE;
	if (!font->valid) return AOS_FALSE;

	font->font_size = size;
	return AOS_TRUE;
}

aos_bool pyrion_switch_off(void) {
    return gdevice->switch_off(gdevice);
}
