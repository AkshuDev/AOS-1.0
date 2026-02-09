#include <inttypes.h>
#include <asm.h>

#include <inc/drivers/core/framebuffer.h>

extern uint8_t font8x16[256][16];

void fb_clear(FB_Info_t* fb, uint32_t color) {
    fb_draw_rect(fb, 0, 0, fb->width, fb->height, color);
}

void fb_put_pixel(FB_Info_t* fb, int x, int y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    uint32_t* pixel = (uint32_t*)((uint8_t*)fb->addr + y * fb->pitch + x * (fb->bpp / 8));
    *pixel = color;
}

void fb_draw_rect(FB_Info_t* fb, int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int k = 0; k < w; k++) {
            fb_put_pixel(fb, x + k, y + j, color);
        }
    }
}

void fb_printc(FB_Info_t* fb, FB_Cursor_t* cur, char c) {
    if (c < 0 || c > 255) return;
    if (cur->x < 0) cur->x = 0;
    if (cur->y < 0) cur->y = 0;

    switch (c) {
        case '\n':
            cur->x = 0;
            cur->y += 16;
            if (cur->y + 16 > fb->height) cur->y = 0;
            return;
        case '\b':
            if (cur->x == 0 && cur->y != 0) {
                cur->x = fb->width;
                cur->y -= 16;
            } else if (cur->x == 0 && cur->y == 0) {
                return;
            } else {
                cur->x -= 8;
            }
            for (int row = 0; row < 16; row++) {
                uint8_t line = font8x16[' '][row];
                for (int col = 0; col < 8; col++) {
                    uint32_t color = (line & (1 << (7 - col))) ? cur->fg_color : cur->bg_color;
                    fb_put_pixel(fb, cur->x + col, cur->y + row, color);
                }
            }
            return;
        default:
            break;
    }

    for (int row = 0; row < 16; row++) {
        uint8_t line = font8x16[(uint8_t)c][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (line & (1 << (7 - col))) ? cur->fg_color : cur->bg_color;
            fb_put_pixel(fb, cur->x + col, cur->y + row, color);
        }
    }

    cur->x += 8;
    if (cur->x + 8 > fb->width) {
        cur->x = 0;
        cur->y += 16;
    }

    if (cur->y + 16 > fb->height) {
        cur->y = 0;
    }
}

void fb_print(FB_Info_t* fb, FB_Cursor_t* cur, const char* str) {
    while (*str) {
        fb_printc(fb, cur, *str);
        str++;
    }
}

void fb_set_cursor(FB_Info_t* fb, FB_Cursor_t* cur, uint32_t x, uint32_t y) {
    cur->x = x;
    cur->y = y;
}


