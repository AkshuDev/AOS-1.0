#include <pbfs_blt_stub.h> // Has everything for bootloaders/protected mode

#define AOS_KERNEL_LOC ((void*)0x100000)
#define AOS_KERNEL_ADDR 0x100000
#define KERNEL_STACK_TOP 0x208000

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

__attribute__((naked, noreturn))
void stage3_jump_to_kernel(void (*kernel)(void), unsigned int stack_top) {
    __asm__ __volatile__ (
        "movl 4(%esp), %eax\n\t" // eax = kernel address
        "movl 8(%esp), %ebx\n\t" // ebx = stack top
        "movl %ebx, %esp\n\t" // set esp
        "movl %ebx, %ebp\n\t" // set ebp
        "cli\n\t" // disable interrupts
        "cld\n\t" // clear direction flag (best practice)
        "jmp *%eax\n\t" // jump directly to kernel
    );
    __builtin_unreachable();
}

void stage3(void) __attribute__((used)); 
void stage3(void) {
    PM_Cursor_t cursor = {
        .x = 0,
        .y = 0,
        .fg = PM_COLOR_WHITE,
        .bg = PM_COLOR_BLACK
    };

    pm_set_cursor(&cursor, 0, 0);
    pm_clear_screen(&cursor);
    pm_print(&cursor, "Welcome To AOS Bootloader!\n");
    
    unsigned char *mem = (unsigned char *)0x100000;
    *mem = 0xAA;
    if (*mem != 0xAA) {
        cursor.fg = PM_COLOR_RED;
        pm_print(&cursor, "A20 line disabled!\n");
        for (;;) asm("hlt");
    }

    pm_print(&cursor, "Loading AOS...\n");
    PBFS_DP dp = {
        .count = 9,
        .lba = 16
    };
    
    int out = pm_read_sectors(&dp, AOS_KERNEL_LOC);
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
    stage3_jump_to_kernel((void(*)(void))AOS_KERNEL_LOC, KERNEL_STACK_TOP);

    __builtin_unreachable(); // Tell GCC control never returns 
}

asm(".globl stage3");
