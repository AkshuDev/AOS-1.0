#pragma once 
#include <inttypes.h>

typedef struct {
    uint64_t addr; // virtual framebuffer address
    uint64_t phys_addr; // physical framebuffer address
    uint32_t width; // screen width in pixels
    uint32_t height; // screen height in pixels
    uint32_t pitch; // bytes per row
    uint8_t bpp; // bits per pixel (usually 32)
    uint64_t size;
} FB_Info_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t fg_color;
    uint32_t bg_color;
} FB_Cursor_t;

void fb_clear(FB_Info_t* fb, uint32_t color) __attribute__((used));
void fb_set_cursor(FB_Info_t* fb, FB_Cursor_t* cur, uint32_t x, uint32_t y) __attribute__((used));
void fb_put_pixel(FB_Info_t* fb, int x, int y, uint32_t color) __attribute__((used));
void fb_draw_rect(FB_Info_t* fb, int x, int y, int w, int h, uint32_t color) __attribute__((used));
void fb_printc(FB_Info_t* fb, FB_Cursor_t* cur, char c) __attribute__((used));
void fb_print(FB_Info_t* fb, FB_Cursor_t* cur, const char* str) __attribute__((used));
void fb_printf(FB_Info_t* fb, FB_Cursor_t* cur, const char* fmt, ...) __attribute__((used));

