#include <stdbool.h>
#include <stdlib.h>
#include "../headers/vga.h"

// Framebuffer (example for OpenGL)
unsigned int framebuffer;

// Function Prototypes
void init_vga_mode();
void draw_pixel_vga(int x, int y, unsigned char color);
void draw_line_vga(int x1, int y1, int x2, int y2, unsigned char color);

void init_opengl_mode();
void draw_pixel_opengl(int x, int y, unsigned int color);
void draw_line_opengl(int x1, int y1, int x2, int y2, unsigned int color);

void draw_pixel(int x, int y, unsigned int color);
void draw_line(int x1, int y1, int x2, int y2, unsigned int color);
void draw_app(int x, int y, int width, int height);

// Determine graphics mode (VGA or OpenGL)
bool is_modern_graphics_supported = false;

int main(int argc, char **argv){
    return 0;
}

// Initialize VGA mode
void init_vga_mode() {
    // Set VGA mode to 320x200 with 256 colors (Mode 13h)
    asm volatile (
        "mov $0x13, %%ax\n"
        "int $0x10\n"
        :
        :
        : "ax"
    );
}

// Draw pixel in VGA mode
void draw_pixel_vga(int x, int y, unsigned char color) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    unsigned char *video_memory = (unsigned char *) VGA_VIDEO_MEMORY;
    video_memory[y * VGA_WIDTH + x] = color;
}

// Draw line in VGA mode (simplified Bresenham’s Algorithm)
void draw_line_vga(int x1, int y1, int x2, int y2, unsigned char color) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
        draw_pixel_vga(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

// Initialize OpenGL mode
void init_opengl_mode() {
    // Assume framebuffer and context setup for OpenGL is done here.
    // This will be hardware/driver dependent and involves managing OpenGL context.
}

// Draw pixel in OpenGL mode (placeholder)
void draw_pixel_opengl(int x, int y, unsigned int color) {
    // OpenGL typically works with shaders, but for simplicity:
    // Bind framebuffer and draw a single pixel (example).
}

// Draw line in OpenGL mode (placeholder)
void draw_line_opengl(int x1, int y1, int x2, int y2, unsigned int color) {
    // Use OpenGL draw commands or shaders to draw the line.
    // This is much more involved in OpenGL.
}

// Unified draw_pixel function (auto-detect VGA or OpenGL)
void draw_pixel(int x, int y, unsigned int color) {
    if (is_modern_graphics_supported) {
        draw_pixel_opengl(x, y, color);
    } else {
        draw_pixel_vga(x, y, (unsigned char) color);  // Color casting for 8-bit VGA
    }
}

// Unified draw_line function (auto-detect VGA or OpenGL)
void draw_line(int x1, int y1, int x2, int y2, unsigned int color) {
    if (is_modern_graphics_supported) {
        draw_line_opengl(x1, y1, x2, y2, color);
    } else {
        draw_line_vga(x1, y1, x2, y2, (unsigned char) color);
    }
}

// Create a basic window (for now just drawing a border)
void draw_app(int x, int y, int width, int height) {
    unsigned int color = 0xFFFFFF; // White color for window border

    // Draw borders of the window
    draw_line(x, y, x + width, y, color);        // Top
    draw_line(x, y, x, y + height, color);       // Left
    draw_line(x + width, y, x + width, y + height, color);  // Right
    draw_line(x, y + height, x + width, y + height, color); // Bottom
}

// Entry point for setting up graphics
void setup_graphics() {
    // If OpenGL is supported, initialize OpenGL mode
    // This could involve checking for hardware support in your OS
    if (/* Modern Graphics Detected */ false) {
        is_modern_graphics_supported = true;
        init_opengl_mode();
    } else {
        // Fallback to VGA
        init_vga_mode();
    }
}
