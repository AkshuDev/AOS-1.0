#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/core/acpi.h>
#include <inc/drivers/io/drive.h>

void pm_print_hex(struct VMemDesign* cursor, unsigned int val) {
    char hex[9];
    const char *digits = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        hex[i] = digits[val & 0xF];
        val >>= 4;
    }
    hex[8] = '\0';
    vmem_print(cursor, hex);
}

void cpuid_get_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    __asm__ volatile ("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(eax));
    *((uint32_t*)vendor_out) = ebx;
    *((uint32_t*)(vendor_out + 4)) = edx;
    *((uint32_t*)(vendor_out + 8)) = ecx;
    vendor_out[12] = 0;
}

uint32_t cpuid_signature(void) {
    uint32_t eax;
    __asm__ volatile ("cpuid"
        : "=a"(eax)
        : "a"(1)
        : "ebx", "ecx", "edx");
    return eax;
}

void enable_a20(void) {
    asm volatile(
        "inb $0x92, %%al\n\t"
        "orb $0x02, %%al\n\t"
        "outb %%al, $0x92\n\t"
        :
        :
        : "al"
    );
}


__attribute__((naked, noreturn))
void stage3_jump_to_kernel(void (*kernel)(void), uint64_t stack_top) {
    __asm__ __volatile__ (
        "mov %rsi, %rsp\n\t" // stack_top is 2nd arg (rsi)
        "mov %rsi, %rbp\n\t"
        "cli\n\t"
        "cld\n\t"
        "jmp *%rdi\n\t" // kernel pointer is 1st arg (rdi)
    );
}

uint8_t check_apic(void) { return 0; } // Stub for now
uint64_t measure_tsc(void) { return 0; } // Stub for now

uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

void stage3(void) __attribute__((section(".entry"), used)); 
void stage3(void) {
    enable_a20();
    struct VMemDesign cursor = {
        .x = 0,
        .y = 0,
        .fg = VMEM_COLOR_WHITE,
        .bg = VMEM_COLOR_BLACK,
        .serial_out = 1
    };

    acpi_init();

    vmem_set_cursor(0, 0);
    vmem_clear_screen(&cursor);
    vmem_print(&cursor, "Welcome To AOS Bootloader! Debugging...\n");
    
    unsigned char *mem = (unsigned char *)0x100000;
    *mem = 0xAA;
    if (*mem != 0xAA) {
        cursor.fg = VMEM_COLOR_RED;
        vmem_print(&cursor, "A20 line disabled!\n");
        for (;;) asm("hlt");
    }

    uint8_t boot_drive = *AOS_BOOT_INFO_LOC; // Get Boot drive
    vmem_print(&cursor, "Updating System info...\n");
    aos_sysinfo_t* SystemInfo = (aos_sysinfo_t*)AOS_SYS_INFO_LOC;
    SystemInfo->boot_drive = boot_drive;
    SystemInfo->boot_mode = 0;
    SystemInfo->reserved0 = 0;
    SystemInfo->cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo->cpu_vendor);
    SystemInfo->apic_present = check_apic();
    SystemInfo->tsc_freq_hz = measure_tsc();
    SystemInfo->checksum = compute_checksum((uint8_t*)SystemInfo, sizeof(*SystemInfo) - 1);

    vmem_print(&cursor, "SystemInfo initialization complete!\n");
    vmem_print(&cursor, "OS to Load: ");
    char os_to_load[256];
    if (is_ps2_present() == 1) {
        ps2_init();
        ps2_read_line(os_to_load, 256, &cursor);
        if (strcmp(os_to_load, "aos") != 0) {
            vmem_print(&cursor, "\nOS Kernel not found!\n");
            for (;;) asm("hlt");
        }
    } else {
        vmem_print(&cursor, "No keyboard detected .... Running AOS\n");
    }
    vmem_print(&cursor, "\n");

    vmem_print(&cursor, "Loading AOS...\n");

    uint8_t found_drive = 0;
    drive_device_t cur_drive = {0};

    struct ATA_DP dp = (struct ATA_DP){
        .count = 150,
        .lba = 400
    };

    if (get_available_drives(&cur_drive) == 1) {
        if (cur_drive.active != 1) {
            if (cur_drive.init != NULL) cur_drive.init();
        }
        if (cur_drive.read_blk != NULL) {
            serial_print("Running Kernel Read...\n");
            found_drive = cur_drive.read_blk(cur_drive.cur_port, dp.lba, dp.count, AOS_KERNEL_LOC);
            serial_printf("Read worked: %d\n", found_drive);
        }
    }

    if (found_drive != 1) {
        vmem_print(&cursor, "No drive found, using ATA!\n");
        
        int out = ata_read_sectors(&dp, AOS_KERNEL_LOC, boot_drive);
        if (out != 0) {
            cursor.fg = VMEM_COLOR_RED;
            vmem_print(&cursor, "Disk Error!\n");
            for (;;) asm("hlt");
        }
    }
    cursor.fg = VMEM_COLOR_GREEN;
    vmem_print(&cursor, "Loaded Kernel!\n");
    cursor.fg = VMEM_COLOR_YELLOW;
    vmem_print(&cursor, "Dumping first 16 dwords from 0x100000:\n");
    unsigned int *data = (unsigned int*)0x100000;
    for (int i = 0; i < 16; i++) {
        pm_print_hex(&cursor, data[i]);
        vmem_print(&cursor, " ");
    }
    vmem_print(&cursor, "\n\n\nJumping to Kernel...\n");
    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

asm(".globl stage3");
