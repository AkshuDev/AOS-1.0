struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned char *address;
};

void put_pixel_framebuffer(int x, int y, unsigned int color, struct framebuffer_info *fb);

void put_pixel_framebuffer(int x, int y, unsigned int color, struct framebuffer_info *fb){
    unsigned int *pixel = (unsigned int)(fb->address + (y * fb->pitch + x * 4)); //4 bytes per pixel (32-bit)
    *pixel = color;
}