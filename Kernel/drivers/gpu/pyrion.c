#include <stdarg.h>

#include <inttypes.h>
#include <asm.h>
#include <inc/core/pcie.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/gpu/gpu.h>

extern uint8_t font8x16[256][16];

static struct gpu_device* gdevice = NULL;

static uint32_t* create_font_atlas_rgba(uint64_t* out_phys) {
    uint32_t width = 8;
    uint32_t height = 256 * 16;
    size_t size = width * height * sizeof(uint32_t);

    uint32_t* atlas = (uint32_t*)avmf_alloc(size, MALLOC_TYPE_USER, PAGE_PRESENT | PAGE_RW, out_phys);
    
    for (int char_idx = 0; char_idx < 256; char_idx++) {
        for (int row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[char_idx][row];
            for (int col = 0; col < 8; col++) {
                // Bits are usually stored MSB-first in these fonts
                int bit = (row_data >> (7 - col)) & 1;
                atlas[(char_idx * 16 + row) * 8 + col] = bit ? 0xFFFFFFFF : 0x00000000;
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

void pyrion_init(struct gpu_device* device) {
    if (!device) return;
    gdevice = device;
    gdevice->pyrion.init();
}

void pyrion_finish(void) {
    gdevice->pyrion.finish();
}

struct pyrion_ctx* pyrion_create_ctx(void) {
    struct pyrion_ctx* ctx = gdevice->pyrion.create_ctx();
    if (!ctx) return NULL;

    ctx->cformat = PYRION_COLORF_RGBA;
    ctx->display_info.width = gdevice->framebuffer->w;
    ctx->display_info.height = gdevice->framebuffer->h;
    ctx->display_info.bpp = gdevice->framebuffer->bpp;
    ctx->display_info.pitch = gdevice->framebuffer->pitch;
    ctx->display_info.size = gdevice->framebuffer->size;

    ctx->font_ready = 0;

    ctx->fb_info.bg_color = 0xFFFFFFFF;
    ctx->fb_info.fg_color = 0x000000FF;
    ctx->fb_info.x = 0;
    ctx->fb_info.y = 0;

    return ctx;
}

void pyrion_destroy_ctx(struct pyrion_ctx *ctx) {
    if (!ctx) return;

    if (ctx->font_ready == 1) {
        if (ctx->font.atlas)
            avmf_free((uint64_t)ctx->font.atlas);
        ctx->font_ready = 0;
    }
    gdevice->pyrion.destroy_ctx(ctx);
}

void pyrion_viewport(struct pyrion_ctx *ctx, struct pyrion_rect *viewport) {
    if (!ctx || !viewport) return;
    gdevice->pyrion.viewport(ctx, viewport);
}

void pyrion_conf(struct pyrion_ctx *ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (!ctx) return;
    ctx->fb_info.x = x;
    ctx->fb_info.y = y;
    ctx->fb_info.fg_color = fg;
    ctx->fb_info.bg_color = bg;
}

void pyrion_flush(struct pyrion_ctx *ctx) {
    if (!ctx) return;
    gdevice->pyrion.flush(ctx);
}

void pyrion_clear(struct pyrion_ctx *ctx, uint32_t color) {
    if (!ctx) return;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, color, &r, &g, &b, &a);
    gdevice->pyrion.clear(ctx, r, g, b, a);
}

void pyrion_pixel(struct pyrion_ctx *ctx, uint32_t x, uint32_t y, uint32_t color) {
    if (!ctx) return;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, color, &r, &g, &b, &a);
    gdevice->pyrion.pixel(ctx, x, y, r, g, b, a);
}

void pyrion_set_cursor(struct pyrion_ctx *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return;
    ctx->fb_info.x = x;
    ctx->fb_info.y = y;
}

void pyrion_builtin_draw_rect(struct pyrion_ctx *ctx, struct pyrion_rect *rect) {
    if (!ctx) return;
    uint8_t r, g, b, a;
    pyrion_extract_rgba(ctx->cformat, rect->color, &r, &g, &b, &a);
    gdevice->pyrion.draw_rect(ctx, rect->x, rect->y, rect->width, rect->height, r, g, b, a);
}

void pyrion_builtin_printc(struct pyrion_ctx *ctx, char c) {
    if (!ctx) return;

    if (ctx->font_ready != 1) {
        ctx->font.atlas = create_font_atlas_rgba(&ctx->font.atlas_phys);
        if (!ctx->font.atlas) return;
        ctx->font.h = 16;
        ctx->font.w = 8;
        ctx->font.total_h = 16 * 256;
        ctx->font.res_id = gdevice->pyrion.upload_font(ctx, ctx->font.atlas_phys, ctx->font.atlas, ctx->font.w, ctx->font.total_h);
        ctx->font_ready = 1;
    }

    if (c == '\n') {
        ctx->fb_info.x = 0;
        ctx->fb_info.y += ctx->font.h;
        return;
    } else if (c == ' ') {
        ctx->fb_info.x += ctx->font.w;
        return;
    }
    
    uint32_t atlas_y = (uint32_t)((uint8_t)c * 16);
    gdevice->pyrion.draw_char(ctx, ctx->fb_info.x, ctx->fb_info.y, 0, atlas_y, ctx->font.w, ctx->font.h, ctx->font.res_id);

    ctx->fb_info.x += ctx->font.w;

    if (ctx->fb_info.x + ctx->font.w > ctx->display_info.width) {
        ctx->fb_info.x = 0;
        ctx->fb_info.y += ctx->font.h;
    }
}

void pyrion_builtin_print(struct pyrion_ctx *ctx, const char *str) {
    if (!ctx) return;

    char* c = str;
    while (*c) {
        pyrion_builtin_printc(ctx, *c);
        c++;
    }
}

static void pyrion_builtin_print_ex_integer(struct pyrion_ctx* ctx, uint64_t val, int base, int width, int zero_pad, int is_signed) {
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
                pyrion_builtin_printc(ctx, '-');
                neg = 0;
            }
            while (padding_count--) pyrion_builtin_printc(ctx, '0');
        } else {
            while(padding_count--) pyrion_builtin_printc(ctx, ' ');
        }
    }

    if (neg) pyrion_builtin_printc(ctx, '-');
    while (i > 0) {
        pyrion_builtin_printc(ctx, buf[--i]);
    }
}

void pyrion_builtin_printf(struct pyrion_ctx *ctx, const char *fmt, ...) {
    if (!ctx) return;

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
                    pyrion_builtin_printc(ctx, c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    pyrion_builtin_print(ctx, s ? s : "(NULL)");
                    break;
                }
                case 'i':
                case 'd': { // signed 32/64-bit
                    int64_t d;
                    if (is_long >= 1) d = va_arg(args, int64_t);
                    else d = (int64_t)va_arg(args, int);
                    pyrion_builtin_print_ex_integer(ctx, (uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    pyrion_builtin_print_ex_integer(ctx, u, 10, width, zero_pad, 0);
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
                    pyrion_builtin_print_ex_integer(ctx, p, 16, width, zero_pad, 0);
                    break;
                }
                case '%': {
                    pyrion_builtin_printc(ctx, '%');
                    break;
                }
                default: {
                    pyrion_builtin_printc(ctx, *fmt);
                    break;
                }
            }
        } else {
            pyrion_builtin_printc(ctx, *fmt);
        }
        fmt++;
    }

    va_end(args);
}

void pyrion_switch_off(void) {

}