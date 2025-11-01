#include <pbfs_blt_stub.h> // Has everything for bootloaders/protected mode
#include <system.h>

void pm_print_hex(PM_Cursor_t *cursor, unsigned int val) {
    char hex[9];
    const char *digits = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        hex[i] = digits[val & 0xF];
        val >>= 4;
    }
    hex[8] = '\0';
    pm_print(cursor, hex);
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
    __builtin_unreachable();
}

uint8_t check_apic(void) { return 0; } // Stub for now
uint64_t measure_tsc(void) { return 0; } // Stub for now

uint8_t compute_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

void stage3(void) __attribute__((used)); 
void stage3(void) {
    enable_a20();
    PM_Cursor_t cursor = {
        .x = 0,
        .y = 0,
        .fg = PM_COLOR_WHITE,
        .bg = PM_COLOR_BLACK
    };

    pm_set_cursor(&cursor, 0, 0);
    pm_clear_screen(&cursor);
    pm_print(&cursor, "Welcome To AOS Bootloader! Debugging...\n");
    
    unsigned char *mem = (unsigned char *)0x100000;
    *mem = 0xAA;
    if (*mem != 0xAA) {
        cursor.fg = PM_COLOR_RED;
        pm_print(&cursor, "A20 line disabled!\n");
        for (;;) asm("hlt");
    }

    uint8_t boot_drive = *AOS_BOOT_INFO_LOC; // Get Boot drive
    pm_print(&cursor, "Updating System info...\n");
    aos_sysinfo_t SystemInfo;
    SystemInfo.boot_drive = boot_drive;
    SystemInfo.boot_mode = 0;
    SystemInfo.reserved0 = 0;
    SystemInfo.total_memory_kib = 0; // 0 means unknown
    SystemInfo.cpu_signature = cpuid_signature();
    cpuid_get_vendor(SystemInfo.cpu_vendor);
    SystemInfo.apic_present = check_apic();
    SystemInfo.tsc_freq_hz = measure_tsc();
    SystemInfo.checksum = compute_checksum((uint8_t*)&SystemInfo, sizeof(SystemInfo) - 1);

    pm_print(&cursor, "SystemInfo initialization complete!\n");

    // Place it
    PBFS_DP dp = {
        .count = AOS_SYSINFO_SPAN,
        .lba = AOS_SYSINFO_LBA
    };
    pm_write_sectors(&dp, &SystemInfo, boot_drive);
    pm_print(&cursor, "OS to Load: ");
    char os_to_load[256];
    pm_read_line(os_to_load, 256, &cursor);
    if (str_eq(os_to_load, "aos") != 1) {
        pm_print(&cursor, "\nOS Kernel not found!\n");
        for (;;) asm("hlt");
    }
    pm_print(&cursor, "\n");

    pm_print(&cursor, "Loading AOS...\n");
    dp.count = 60;
    dp.lba = 16;
    
    int out = pm_read_sectors(&dp, AOS_KERNEL_LOC, boot_drive);
    if (out != 0) {
        cursor.fg = PM_COLOR_RED;
        pm_print(&cursor, "Disk Error!\n");
        for (;;) asm("hlt");
    }
    cursor.fg = PM_COLOR_GREEN;
    pm_print(&cursor, "Loaded Kernel!\n");
    cursor.fg = PM_COLOR_YELLOW;
    pm_print(&cursor, "Dumping first 16 dwords from 0x100000:\n");
    unsigned int *data = (unsigned int*)0x100000;
    for (int i = 0; i < 16; i++) {
        pm_print_hex(&cursor, data[i]);
        pm_print(&cursor, " ");
    }
    pm_print(&cursor, "\n\n\nJumping to Kernel...\n");
    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, AOS_KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

asm(".globl stage3");
