#include "../../headers/vga.h"

void set_vga_mode() {
    asm volatile (
        "int $0x10" // BIOS interrupt for video services
        : // No outputs
        : "a" (0x0013) // Set video mode to 320x200 with 256 colors (0x13)
    );
}

void put_pixel(int x, int y, unsigned char color) {
    unsigned char *vga = (unsigned char*)0xA0000;  // Video memory location for mode 0x13
    vga[y * VGA_WIDTH + x] = color;
}

int main() {
    set_vga_mode();  // Set graphics mode
    put_pixel(100, 100, 15);  // Plot a white pixel at (100, 100)
    while (1);  // Keep the mode running
    return 0;
}
