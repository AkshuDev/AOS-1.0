#pragma once 
#include <inttypes.h>

typedef struct {
    uint32_t* addr; // physical framebuffer address
    uint32_t width; // screen width in pixels
    uint32_t height; // screen height in pixels
    uint32_t pitch; // bytes per row
    uint8_t bpp; // bits per pixel (usually 32)
} FB_Info_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t fg_color;
    uint32_t bg_color;
} FB_Cursor_t;

void fb_init(uint32_t* framebuffer, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) __attribute__((used));
void fb_clear(uint32_t color) __attribute__((used));
void fb_set_cursor(FB_Cursor_t* cur, uint32_t x, uint32_t y) __attribute__((used));
void fb_put_pixel(int x, int y, uint32_t color) __attribute__((used));
void fb_draw_rect(int x, int y, int w, int h, uint32_t color) __attribute__((used));
void fb_printc(FB_Cursor_t* cur, char c) __attribute__((used));
void fb_print(FB_Cursor_t* cur, const char* str) __attribute__((used));
void fb_printf(FB_Cursor_t* cur, const char* fmt, ...) __attribute__((used));

