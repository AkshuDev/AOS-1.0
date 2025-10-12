#include <inttypes.h>
#include <system.h>
#include <asm.h>
#include <fonts.h>

#include <inc/acpi.h>
#include <inc/framebuffer.h>
#include <inc/io.h>
#include <inc/pcie.h>

void aospp_start() __attribute__((used));
void aospp() __attribute__((used));

static uint32_t fb_addr = 0; 
static FB_Cursor_t fb_cur = {
    .x = 0,
    .y = 0,
    .fg_color = 0xFFFFFF,
    .bg_color = 0x000000
};

void aospp_start() {
    serial_init();
    serial_print("AOS++ LOADED!\n");

    fb_addr = pcie_get_vbe_framebuffer();
    if (fb_addr == 0) {
        serial_print("Failed to get framebuffer\n");
        return;
    }

    serial_printf("Framebuffer addr: 0x%x\n", fb_addr);
 

    fb_init((uint32_t*)fb_addr, 1024, 768, 1024*4, 32);
    
    fb_clear();

    fb_print(&fb_cur, "Hello this is AOS++ Shell, More modern using Framebuffer!\n");
}
