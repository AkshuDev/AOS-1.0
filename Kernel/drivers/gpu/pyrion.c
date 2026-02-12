#include <stdarg.h>

#include <inttypes.h>
#include <asm.h>

#include <inc/drivers/core/gpu.h>
#include <inc/core/pcie.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/io/io.h>

#include <inc/drivers/gpu/apis/pyrion.h>

static struct gpu_device* gdevice = NULL;
static struct pyrion_display_info gdisplay_info = {0};
static uint64_t gcur_ctx_id = 0;
static uint64_t gcur_resource_id = 1;

void pyrion_init(struct gpu_device* device) {
    gdevice = device;
    gdisplay_info.width = device->framebuffer->w;
    gdisplay_info.height = device->framebuffer->h;
    gdisplay_info.pitch = device->framebuffer->pitch;
    gdisplay_info.bpp = device->framebuffer->bpp;
    gdisplay_info.size = device->framebuffer->size;
    gdisplay_info.color_format = PYRION_CFORMAT_A8B8G8R8;
}

struct pyrion_ctx* pyrion_create_ctx(void) {
    uint64_t ctx_phys = 0;
    uint64_t ctx_virt = (uint64_t)avmf_alloc(sizeof(struct pyrion_ctx), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &ctx_phys);

    if (ctx_virt == NULL) return NULL;
    struct pyrion_ctx* ctx = (struct pyrion_ctx*)ctx_virt;

    ctx->ctx_phys = ctx_phys;
    ctx->ctx_id = gcur_ctx_id++;
    ctx->resource_id = gcur_resource_id++;
    
    ctx->viewport.x = 0; ctx->viewport.y=0; ctx->viewport.width=0; ctx->viewport.height; ctx->viewport.color = 0;
    ctx->fb_cursor.x = 0; ctx->fb_cursor.y = 0; ctx->fb_cursor.fg_color = 0; ctx->fb_cursor.bg_color = 0;
    ctx->fb_info.addr = 0; ctx->fb_info.phys_addr = 0; ctx->fb_info.width = 0; ctx->fb_info.height = 0; ctx->fb_info.pitch = 0;
    ctx->fb_info.size = 0; ctx->fb_info.bpp = gdisplay_info.bpp;
    
    ctx->valid = 0;
    return ctx;
}

void pyrion_viewport(struct pyrion_ctx* ctx, struct pyrion_rect* viewport) {
    if (ctx == NULL || ctx->fb_info.addr != 0 || ctx->fb_info.phys_addr != 0) return; // Only allows a single time use per context
    if (!(
        viewport->x >= 0 && 
        viewport->x <= gdisplay_info.width &&
        viewport->y >= 0 &&
        viewport->y <= gdisplay_info.height &&
        viewport->width >= 0 &&
        viewport->width <= gdisplay_info.width &&
        viewport->height >= 0 &&
        viewport->height <= gdisplay_info.height
    )) return;

    ctx->fb_info.addr = avmf_alloc(viewport->width * viewport->height * sizeof(uint32_t), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &ctx->fb_info.phys_addr);
    if (ctx->fb_info.addr == NULL) return;
    ctx->fb_info.width = viewport->width;
    ctx->fb_info.height = viewport->height;
    ctx->fb_info.pitch = viewport->width * (ctx->fb_info.bpp / 8);
    ctx->fb_info.size = (uint64_t)(viewport->height * ctx->fb_info.pitch);

    ctx->fb_cursor.x = 0;
    ctx->fb_cursor.y = 0;
    ctx->fb_cursor.fg_color = 0x000000FF;
    ctx->fb_cursor.bg_color = viewport->color;

    ctx->viewport = *viewport;
}

void pyrion_flush(struct pyrion_ctx* ctx) {
    if (ctx == NULL || ctx->fb_info.addr == 0 || ctx->fb_info.phys_addr == 0) return; // Invalid
    struct pyrion_rect* viewport = &ctx->viewport;
    if (!(
        viewport->x >= 0 && 
        viewport->x <= gdisplay_info.width &&
        viewport->y >= 0 &&
        viewport->y <= gdisplay_info.height &&
        viewport->width >= 0 &&
        viewport->width <= gdisplay_info.width &&
        viewport->height >= 0 &&
        viewport->height <= gdisplay_info.height
    )) return;

    uint8_t bpp = ctx->fb_info.bpp / 8;
    uint8_t* src = (uint8_t*)ctx->fb_info.addr;
    uint8_t* dest = (uint8_t*)(gdevice->framebuffer->virt);
    dest += (viewport->y * gdisplay_info.pitch) + (viewport->x * bpp);
    for (uint32_t i = 0; i < viewport->height; i++) {
        memcpy(dest, src, viewport->width * bpp);
        src += ctx->fb_info.pitch;
        dest += gdisplay_info.pitch;
    }

    if (gdevice->flush != NULL) gdevice->flush(gdevice, viewport->x, viewport->y, viewport->width, viewport->height, 1);
}

void pyrion_builtin_printc(struct pyrion_ctx* ctx, char c) {
    if (ctx == NULL) return;
    fb_printc(&ctx->fb_info, &ctx->fb_cursor, c);
}

void pyrion_builtin_print(struct pyrion_ctx* ctx, const char* str) {
    if (ctx == NULL) return;
    fb_print(&ctx->fb_info, &ctx->fb_cursor, str);
}

static uint64_t udiv64(uint64_t n, uint64_t d) {
    uint64_t q = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        q <<= 1;
        if ((n >> i) >= d) {
            n -= d << i;
            q |= 1;
        }
    }
    return q;
}
static uint64_t umod64(uint64_t n, uint64_t d) {
    int i;
    for (i = 63; i >= 0; i--) {
        if ((n >> i) >= d) {
            n -= d << i;
        }
    }
    return n;
}

static void pyrion_print_integer(struct pyrion_ctx* ctx, uint64_t val, int is_signed, int base, int uppercase) {
    char buffer[32];
    int i = 30;
    buffer[31] = 0;
    int neg = 0;

    if (is_signed && ((int64_t)val) < 0) {
        neg = 1;
        val = -(int64_t)val;
    }

    do {
        uint64_t digit = umod64(val, base);
        buffer[i--] = (digit < 10) ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
        val = udiv64(val, base);
    } while (val);

    if (neg) buffer[i--] = '-';
    pyrion_builtin_print(ctx, &buffer[i+1]);
}

static void pyrion_print_ex_integer(struct pyrion_ctx* ctx, uint64_t val, int base, int width, int zero_pad, int is_signed) {
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

void pyrion_builtin_printf(struct pyrion_ctx* ctx, const char* fmt, ...) {
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
                    pyrion_print_ex_integer(ctx, (uint64_t)d, 10, width, zero_pad, 1);
                    break;
                }
                case 'u': { // unsigned 32/64-bit
                    uint64_t u;
                    if (is_long >= 1) u = va_arg(args, uint64_t);
                    else u = (uint64_t)va_arg(args, uint32_t);
                    pyrion_print_ex_integer(ctx, u, 10, width, zero_pad, 0);
                    break;
                }
                case 'x':
                case 'p': { // Pointer
                    uint64_t p;
                    if (*fmt == 'p') {
                        p = (uintptr_t)va_arg(args, void*);
                        pyrion_builtin_print(ctx, "0x");
                        if (width == 0) width = 16;
                        zero_pad = 1;
                    } else {
                        if (is_long >= 1) p = va_arg(args, uint64_t);
                        else p = (uint64_t)va_arg(args, uint32_t);
                    }
                    pyrion_print_ex_integer(ctx, p, 16, width, zero_pad, 0);
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

void pyrion_builtin_draw_rect(struct pyrion_ctx* ctx, struct pyrion_rect* rect) {
    if (ctx == NULL) return;
    fb_draw_rect(&ctx->fb_info, rect->x, rect->y, rect->width, rect->height, rect->color);
}

void pyrion_pixel(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t color) {
    if (ctx == NULL) return;
    fb_put_pixel(&ctx->fb_info, x, y, color);
}

void pyrion_clear(struct pyrion_ctx* ctx, uint32_t color) {
    if (ctx == NULL) return;
    fb_clear(&ctx->fb_info, color);
}

void pyrion_set_cursor(struct pyrion_ctx* ctx, uint32_t x, uint32_t y) {
    if (ctx == NULL) return;
    if (x > ctx->viewport.width || x < 0 || y > ctx->viewport.height || y < 0) return;
    ctx->fb_cursor.x = x;
    ctx->fb_cursor.y = y;
}

void pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (ctx == NULL) return;
    ctx->fb_cursor.fg_color = fg;
    ctx->fb_cursor.bg_color = bg;
    ctx->fb_cursor.x = x;
    ctx->fb_cursor.y = y;
}

