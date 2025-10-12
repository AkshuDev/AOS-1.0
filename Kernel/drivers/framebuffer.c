#include <inttypes.h>
#include <asm.h>

#include <inc/framebuffer.h>

extern uint8_t font8x16[256][16];
static FB_Info_t FramebufferInfo;

void fb_init(uint32_t* framebuffer, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
    FramebufferInfo.addr = framebuffer;
    FramebufferInfo.width = width;
    FramebufferInfo.height = height;
    FramebufferInfo.pitch = pitch;
    FramebufferInfo.bpp = bpp;
}

void fb_clear(void) {
    fb_draw_rect(0, 0, FramebufferInfo.width, FramebufferInfo.height, 0x000000); // black color
}

void fb_put_pixel(int x, int y, uint32_t color) {
    if (x >= FramebufferInfo.width || y >= FramebufferInfo.height) return;
    uint32_t* pixel = (uint32_t*)((uint8_t*)FramebufferInfo.addr + y * FramebufferInfo.pitch + x * (FramebufferInfo.bpp / 8));
    *pixel = color;
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int k = 0; k < w; k++) {
            fb_put_pixel(x + k, y + j, color);
        }
    }
}

void fb_printc(FB_Cursor_t* cur, char c) {
    if (c < 0 || c > 255) return;

    for (int row = 0; row < 16; row++) {
        uint8_t line = font8x16[(uint8_t)c][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (line & (1 << (7 - col))) ? cur->fg_color : cur->bg_color;
            fb_put_pixel(cur->x + col, cur->y + row, color);
        }
    }

    cur->x += 8;
    if (cur->x + 8 > FramebufferInfo.width) {
        cur->x = 0;
        cur->y += 16;
    }

    if (cur->y + 16 > FramebufferInfo.height) {
        cur->y = 0;
    }
}

void fb_print(FB_Cursor_t* cur, const char* str) {
    while (*str) {
        if (*str == '\n') {
            cur->x = 0;
            cur->y += 16;
            if (cur->y + 16 > FramebufferInfo.height) cur->y = 0;
        } else {
            fb_printc(cur, *str);
        }
        str++;
    }
}

void fb_set_cursor(FB_Cursor_t* cur, uint32_t x, uint32_t y) {
    cur->x = x;
    cur->y = y;
}


