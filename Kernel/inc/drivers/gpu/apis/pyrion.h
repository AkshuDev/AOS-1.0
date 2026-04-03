#pragma once

#include <inttypes.h>
#include <inc/drivers/core/framebuffer.h>

enum pyrion_color_format {
    PYRION_COLORF_RGBA,
    PYRION_COLORF_BGRA,
    PYRION_COLORF_ABGR,
    PYRION_COLORF_ARGB,
    PYRION_COLORF_RGB, // A is always 0xFF
    PYRION_COLORF_BGR, // A is always 0xFF
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
    uint8_t padding;
};

struct pyrion_font {
    uint32_t* atlas;
    uint64_t atlas_phys;
    uint32_t res_id;
    uint32_t w;
    uint32_t h;
    uint32_t total_h;
};

struct pyrion_ctx {
    uint64_t ctx_id;
    uint64_t res_id;

    uint64_t ctx_phys;

    FB_Cursor_t fb_info;
    struct pyrion_display_info display_info;
    enum pyrion_color_format cformat;

    uint8_t font_ready;
    struct pyrion_font font;
    
    void* driver_data;
    void* driver_data2;
    uint64_t driver_data_phys;
    uint64_t driver_data_phys2;
    uint64_t driver_var;

    struct pyrion_rect viewport;
    uint8_t valid;
};

struct pyrion_api {
    void (*init)(void);
    void (*finish)(void);
    
    struct pyrion_ctx* (*create_ctx)(void);
    void (*destroy_ctx)(struct pyrion_ctx* ctx);

    void (*flush)(struct pyrion_ctx* ctx);
    void (*viewport)(struct pyrion_ctx* ctx, struct pyrion_rect* viewport);

    void (*clear)(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void (*pixel)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void (*draw_rect)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    uint32_t (*upload_font)(struct pyrion_ctx* ctx, uint64_t atlas_phys, uint32_t* atlas, uint32_t atlas_w, uint32_t atlas_total_h);
    void (*destroy_font)(uint32_t font_res_id, void* font_mem);

    void (*draw_char)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t atlas_x, uint32_t atlas_y, uint32_t w, uint32_t h, uint32_t font_res_id);
};

struct gpu_device;

void pyrion_init(struct gpu_device* device) __attribute__((used));
void pyrion_finish(void) __attribute__((used));
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
void pyrion_set_cursor(struct pyrion_ctx* ctx, uint32_t x, uint32_t y) __attribute__((used));

// SWITCH OFF THE GPU, NOT APPLICABLE TO USER APPLICATIONS
void pyrion_switch_off(void) __attribute__((used));