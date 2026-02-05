#pragma once

#include <inttypes.h>
#include <inc/drivers/core/gpu.h>
#include <inc/drivers/core/framebuffer.h>

enum pyrion_color_formats {
    PYRION_CFORMAT_B8G8R8A8,
    PYRION_CFORMAT_A8B8G8R8,
    PYRION_CFORMAT_R8G8B8A8
};

struct pyrion_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
};

struct pyrion_display_info {
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint32_t pitch;
    uint32_t size;
    enum pyrion_color_formats color_format;
    uint8_t padding;
};

struct pyrion_ctx {
    uint32_t ctx_id;
    uint32_t resource_id;
    uint64_t ctx_phys;
    FB_Cursor_t fb_cursor;
    FB_Info_t fb_info;

    struct pyrion_rect viewport;
    uint8_t valid;
};

void pyrion_init(struct gpu_device* device) __attribute__((used));
void pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) __attribute__((used));

struct pyrion_ctx* pyrion_create_ctx(void) __attribute__((used));
void pyrion_destroy_ctx(struct pyrion_ctx* ctx) __attribute__((used));

void pyrion_builtin_printc(struct pyrion_ctx* ctx, char c) __attribute__((used));
void pyrion_builtin_print(struct pyrion_ctx* ctx, const char* str) __attribute__((used));
void pyrion_builtin_printf(struct pyrion_ctx* ctx, const char* fmt, ...) __attribute__((used));
void pyrion_builtin_draw_rect(struct pyrion_ctx* ctx, struct pyrion_rect* rect) __attribute__((used));

void pyrion_clear(struct pyrion_ctx* ctx, uint32_t color) __attribute__((used));
void pyrion_pixel(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t color) __attribute__((used));
void pyrion_flush(struct pyrion_ctx* ctx); __attribute__((used));
void pyrion_viewport(struct pyrion_ctx* ctx, struct pyrion_rect* viewport) __attribute__((used));

