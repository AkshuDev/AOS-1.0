#include <stdint.h>

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MEMORY 0xA0000

// Function to set a pixel on the screen at (x, y) with a color
void put_pixel_vga(int x, int y, unsigned char color) {
    unsigned char *vga = (unsigned char*)VGA_MEMORY;  // Video memory location for mode 0x13
    vga[y * VGA_WIDTH + x] = color;
}

// Clear the VGA screen by setting all pixels to black (0)
void clear_screen_vga() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            put_pixel_vga(x, y, 0);  // Black color
        }
    }
}
