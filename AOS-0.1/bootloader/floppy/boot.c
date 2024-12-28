#include <stdint.h>

#define VIDEO_MEMORY 0xB8000  // Video memory address for text mode

// Forward declaration of assembly function
extern void read_sector(unsigned char drive, unsigned char head, unsigned char cylinder, unsigned char sector, void* buffer);

// Simple function to print a string to the screen in text mode
void print_string(const char *str) {
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    while (*str) {
        *video = (unsigned char)*str | 0x0F00;  // 0x0F00 is the color (white)
        str++;
        video++;
    }
}

// Main function for bootloader
void loadKernel() {
    print_string("Bootloader started...\n");

    // Create a buffer to store the sector data
    unsigned char drive = 0;      // Floppy drive A:
    unsigned char head = 0;       // First head
    unsigned char cylinder = 0;   // Cylinder 0
    unsigned char sector = 2;     // Start reading from sector 2 (typically the first sector after the boot sector)

    unsigned char buffer[512];    // Buffer to store the sector data (typically one sector is 512 bytes)

    // Read sector 1 from the floppy drive
    read_sector(drive, head, cylinder, sector, buffer);

    // Check if the sector was loaded correctly
    print_string("Sector loaded from floppy!\n");

    // Infinite loop (to stop execution after loading)
    while (1) {}
}
