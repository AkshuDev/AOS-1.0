#include <asm.h>

#include <inc/acpi.h>

void acpi_reboot() {
    asm volatile("cli");
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = asm_inb(0x64);
    }
    asm_outb(0x64, 0xFE);
    asm volatile ("hlt");
}
