#include <stdint.h>

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned char *address;
};

// Function to set a pixel at (x, y) with a specified color in the framebuffer
void put_pixel_framebuffer(int x, int y, unsigned int color, struct framebuffer_info *fb) {
    unsigned int *pixel = (unsigned int*)(fb->address + (y * fb->pitch + x * 4)); // 4 bytes per pixel (32-bit)
    *pixel = color;
}

// Clear the entire framebuffer by setting all pixels to black (0)
void clear_screen(struct framebuffer_info *fb) {
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            put_pixel_framebuffer(x, y, 0x000000, fb);  // Black color
        }
    }
}
