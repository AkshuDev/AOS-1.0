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

// Pyrion Font Structure
struct pyrion_font {
    uint32_t* atlas; // Atlas data in RAM
    uint64_t atlas_phys; // Atlas data in RAM (Physical Address)
    uint32_t res_id; // Resource-ID of Font
    uint32_t w; // Width of Font
    uint32_t h; // Height of Font
    uint32_t total_h; // Total Height of Font
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

// Pyrion Context Structure
struct pyrion_ctx {
    uint64_t ctx_id; // Context ID

    uint64_t res_id_3d; // Resource ID of 3D Texture (if present)
	uint64_t res_id_2d; // Resource ID of 2D Texture
	uint64_t res_id_scanout; // Scanout/Surface Handle
	uint64_t res_id_rast; // Rasterization Handle

	uint64_t controller_idx; // Device Driver Index

    FB_Info_t fb_info; // Framebuffer Information
    FB_Cursor_t fb_cursor; // Framebuffer Cursor
    struct pyrion_display_info display_info; // Display Information
    enum pyrion_color_format cformat; // Color Format

    aos_bool font_ready; // Built-In Font Ready (Internal)
    struct pyrion_font font; // Built-In Font (Internal)
    
    void* cmd_buf; // Default Command Buffer
	uint64_t cmd_buf_phys; // Default Command Buffer Physical address
	uint64_t cmd_size; // Default Command Buffer Size (in bytes)
	uint64_t cmd_cap; // Default Command Buffer Capacity (in bytes)

    struct pyrion_rect viewport; // Viewport
	aos_bool viewport_set; // Viewport is initialized

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
	void (*pyrion_unuse_device)(struct pyrion_ctx* ctx);

	aos_bool (*pyrion_enumerate_physical_devices)(size_t* count, size_t idx, struct pyrion_physical_device* out);
	aos_bool (*pyrion_use_device)(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev, struct pyrion_rect viewport);

    aos_bool (*flush)(struct pyrion_ctx* ctx);
    aos_bool (*viewport)(struct pyrion_ctx* ctx, struct pyrion_rect viewport);

    aos_bool (*clear)(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    aos_bool (*pixel)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    aos_bool (*draw_rect)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    uint32_t (*upload_font)(struct pyrion_ctx* ctx, uint64_t atlas_phys, uint32_t* atlas, uint32_t atlas_w, uint32_t atlas_total_h);
    void (*destroy_font)(struct pyrion_ctx* ctx, uint32_t font_res_id, void* font_mem);

    aos_bool (*draw_char)(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t atlas_x, uint32_t atlas_y, uint32_t w, uint32_t h, uint32_t font_res_id);
};

struct gpu_device;

aos_bool pyrion_init(struct gpu_device* device) __attribute__((used));
void pyrion_finish(void) __attribute__((used));
aos_bool pyrion_conf(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) __attribute__((used));

struct pyrion_ctx* pyrion_create_ctx(struct pyrion_create_ctx_info ctx_info) __attribute__((used));
void pyrion_destroy_ctx(struct pyrion_ctx* ctx) __attribute__((used));
void pyrion_unuse_device(struct pyrion_ctx* ctx) __attribute__((used));

aos_bool pyrion_enumerate_physical_devices(size_t* count, size_t idx, struct pyrion_physical_device* out) __attribute__((used));
aos_bool pyrion_use_device(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev, struct pyrion_rect viewport) __attribute__((used));

aos_bool pyrion_builtin_printc(struct pyrion_ctx* ctx, char c) __attribute__((used));
aos_bool pyrion_builtin_print(struct pyrion_ctx* ctx, const char* str) __attribute__((used));
aos_bool pyrion_builtin_printf(struct pyrion_ctx* ctx, const char* fmt, ...) __attribute__((used));
aos_bool pyrion_builtin_draw_rect(struct pyrion_ctx* ctx, struct pyrion_rect rect) __attribute__((used));

aos_bool pyrion_clear(struct pyrion_ctx* ctx, uint32_t color) __attribute__((used));
aos_bool pyrion_pixel(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t color) __attribute__((used));
aos_bool pyrion_flush(struct pyrion_ctx* ctx); __attribute__((used));
aos_bool pyrion_viewport(struct pyrion_ctx* ctx, struct pyrion_rect viewport) __attribute__((used));
aos_bool pyrion_set_cursor(struct pyrion_ctx* ctx, uint32_t x, uint32_t y) __attribute__((used));

// SWITCH OFF THE GPU, NOT APPLICABLE TO USER APPLICATIONS
aos_bool pyrion_switch_off(void) __attribute__((used));