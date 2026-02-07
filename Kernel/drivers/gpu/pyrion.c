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

void pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (ctx == NULL) return;
    ctx->fb_cursor.fg_color = fg;
    ctx->fb_cursor.bg_color = bg;
    ctx->fb_cursor.x = x;
    ctx->fb_cursor.y = y;
}

