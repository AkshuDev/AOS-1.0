#pragma once

#include <aos_inttypes.h>
#include <inc/drivers/gpu/apis/pyrion_color.h>
#include <inc/drivers/core/framebuffer.h>

#define PYRION_MAX_NAME 64

// Pyrion Rectangle Structure
struct pyrion_rect {
    uint32_t x; // X-Coordinate
    uint32_t y; // Y-Coordinate
    uint32_t width; // Width
    uint32_t height; // Height
    uint32_t color; // Color
};

// Pyrion Display Information Structure
struct pyrion_display_info {
    uint32_t width; // Width of screen
    uint32_t height; // Height of screen

    uint8_t bpp; // Bits per pixel
    uint32_t pitch; // Pitch (Number of Bytes per row)

    uint32_t size; // Total Size of entire framebuffer for this display (in bytes)
    uint8_t padding; // Padding
};

// Pyrion Glyph Structure
struct pyrion_glyph {
    uint32_t codepoint; // Unicode Character attached

    uint16_t atlas_x; // Glyph X Coordinate in Atlas
    uint16_t atlas_y; // Glyph Y Coordinate in Atlas

    uint16_t width; // Glyph Width
    uint16_t height; // Glyph Height

    int16_t bearing_x; // Glyph X Bearing
    int16_t bearing_y; // Glyph Y Bearing

    int16_t advance_x; // Glyph - X Advancement value after rendering
    int16_t advance_y; // Glyph - Y Advancement value after rendering

	aos_bool valid; // Is it a valid glyph
};

// Pyrion Font Structure
struct pyrion_font {
    uint8_t* atlas; // Atlas data in RAM
    uint64_t atlas_phys; // Atlas data in RAM (Physical Address)
	
    uint32_t res_id; // Resource-ID of Font

    uint32_t w; // Width of Font Atlas
    uint32_t h; // Height of Font Atlas
	
	int16_t line_height; // Line Height of the Font
    int16_t line_gap; // Line Gap of the Font

	uint64_t glyph_count; // Number of Glyphs present
    struct pyrion_glyph* glyphs; // Glyphs
	struct pyrion_glyph fallback_glyph; // A Glyph that can be used if no matching glyph is found, to symbolize unknown

	uint32_t base_font_size; // The True Logical Font size of the font
	uint32_t font_size; // The Logical Font size of the font

	aos_bool valid; // Font is Valid
};

// Pyrion Create Info Structure
struct pyrion_create_ctx_info {
	char name[PYRION_MAX_NAME]; // Name of Context 
};

// Pyrion Physical Device Structure
struct pyrion_physical_device {
	char name[PYRION_MAX_NAME]; // Name of device
	uint64_t idx; // Index of device

	aos_bool accelerated; // 3D Acceleration present
	size_t cmd_core; // Extra Async Core Index for Command Submission (0xFFFF is none)
};

// Pyrion Command Stream Structure
struct pyrion_cmd_stream {
	uint64_t stream_phys; // Command Stream Physical Address
	void* stream; // Command Stream Pointer

	uint64_t stream_cap; // Command Stream Capacity (in bytes)
	uint64_t stream_count; // Command Stream Count (in bytes)
};

// Pyrion Context Structure
struct pyrion_ctx {
    uint64_t ctx_id; // Context ID

    uint64_t res_id_3d; // Resource ID of 3D Texture (if present)
	uint64_t res_id_2d; // Resource ID of 2D Texture
	uint64_t res_id_scanout; // Scanout/Surface Handle
	uint64_t res_id_rast; // Rasterization Handle
	uint64_t res_id_blend; // Blend State Handle

	uint64_t controller_idx; // Device Driver Index

    FB_Info_t fb_info; // Framebuffer Information
    FB_Cursor_t fb_cursor; // Framebuffer Cursor
    struct pyrion_display_info display_info; // Display Information
    enum pyrion_color_format cformat; // Color Format

    struct pyrion_font font; // Built-In Font
    
    struct pyrion_cmd_stream cmd_stream; // Default Command Stream

    struct pyrion_rect viewport; // Viewport
	aos_bool viewport_set; // Viewport is initialized

	aos_bool flush_needed; // A Dirty Marker, which lets flush optimise and skip non-required flushes

	struct pyrion_physical_device device; // Physical Device being used
	aos_bool device_set; // Physical Device Structures Initialized

    aos_bool valid; // Context-Valid
	aos_bool usable; // Context-Usable
};

// Pyrion API Structure
struct pyrion_api {
    aos_bool (*init)(void);
    void (*finish)(void);
    
    struct pyrion_ctx* (*create_ctx)(struct pyrion_create_ctx_info ctx_info);
    void (*destroy_ctx)(struct pyrion_ctx* ctx);
	void (*unuse_device)(struct pyrion_ctx* ctx);

	aos_bool (*enumerate_physical_devices)(size_t* count, size_t idx, struct pyrion_physical_device* out);
	aos_bool (*use_device)(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev);

	aos_bool (*submit_cmd_stream)(struct pyrion_ctx* ctx, struct pyrion_cmd_stream* stream);

    aos_bool (*flush)(struct pyrion_ctx* ctx);
    aos_bool (*viewport)(struct pyrion_ctx* ctx, struct pyrion_rect viewport);

    aos_bool (*clear)(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    aos_bool (*pixel)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    aos_bool (*draw_rect)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    aos_bool (*upload_font)(struct pyrion_ctx* ctx, struct pyrion_font font, struct pyrion_font* out);
    void (*destroy_font)(struct pyrion_ctx* ctx, struct pyrion_font* font);

    aos_bool (*draw_char)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t codepoint, struct pyrion_font* font, struct pyrion_glyph** out_glyph);
	aos_bool (*blit)(struct pyrion_ctx *ctx, uint32_t dst_res, uint32_t src_res, uint32_t width, uint32_t height);
};

struct gpu_device;

aos_bool pyrion_init(struct gpu_device* device) __attribute__((used));
void pyrion_finish(void) __attribute__((used));
aos_bool pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) __attribute__((used));

struct pyrion_ctx* pyrion_create_ctx(struct pyrion_create_ctx_info ctx_info) __attribute__((used));
void pyrion_destroy_ctx(struct pyrion_ctx* ctx) __attribute__((used));
void pyrion_unuse_device(struct pyrion_ctx* ctx) __attribute__((used));

aos_bool pyrion_enumerate_physical_devices(size_t* count, size_t idx, struct pyrion_physical_device* out) __attribute__((used));
aos_bool pyrion_use_device(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev) __attribute__((used));

aos_bool pyrion_submit_cmd_stream(struct pyrion_ctx* ctx, struct pyrion_cmd_stream* stream) __attribute__((used));

aos_bool pyrion_builtin_printc(struct pyrion_ctx* ctx, char c) __attribute__((used));
aos_bool pyrion_builtin_print(struct pyrion_ctx* ctx, const char* str) __attribute__((used));
aos_bool pyrion_builtin_printf(struct pyrion_ctx* ctx, const char* fmt, ...) __attribute__((used));
aos_bool pyrion_builtin_draw_rect(struct pyrion_ctx* ctx, struct pyrion_rect rect) __attribute__((used));

aos_bool pyrion_set_font_size(struct pyrion_ctx* ctx, struct pyrion_font* font, uint32_t size) __attribute__((used));
aos_bool pyrion_upload_font(struct pyrion_ctx* ctx, struct pyrion_font font, struct pyrion_font* out) __attribute__((used));
void pyrion_destroy_font(struct pyrion_ctx* ctx, struct pyrion_font* font) __attribute__((used));
aos_bool pyrion_set_builtins_font(struct pyrion_ctx* ctx, struct pyrion_font* font)  __attribute__((used));
aos_bool pyrion_set_default_builtins_font(struct pyrion_ctx* ctx)  __attribute__((used));
aos_bool pyrion_set_builtins_font_size(struct pyrion_ctx* ctx, uint32_t size) __attribute__((used));

aos_bool pyrion_clear(struct pyrion_ctx* ctx, uint32_t color) __attribute__((used));
aos_bool pyrion_pixel(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t color) __attribute__((used));
aos_bool pyrion_flush(struct pyrion_ctx* ctx); __attribute__((used));
aos_bool pyrion_viewport(struct pyrion_ctx* ctx, struct pyrion_rect viewport) __attribute__((used));
aos_bool pyrion_set_cursor(struct pyrion_ctx* ctx, uint32_t x, uint32_t y) __attribute__((used));
aos_bool pyrion_blit(struct pyrion_ctx *ctx, uint32_t dst_res, uint32_t src_res, uint32_t width, uint32_t height) __attribute__((used));

// SWITCH OFF THE GPU, NOT APPLICABLE TO USER APPLICATIONS
aos_bool pyrion_switch_off(void) __attribute__((used));