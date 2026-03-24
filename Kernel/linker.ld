ENTRY(kernel_main)

SECTIONS
{
    /* Force the very start of the kernel at 0x100000 */
    . = 0x100000;

    /* Kernel entry section */
    .start : {
        KEEP(*(.start)) /* kernel_main goes here */
    }

    /* All other code, including PBFS helpers */
    . = ALIGN(0x1000);
    .text : {
        *(.text*) /* everything else goes after .start */
    }

    . = ALIGN(0x1000);
    .rodata : { *(.rodata*) }

    . = ALIGN(0x1000);
    .data   : { *(.data*) }

    . = ALIGN(0x1000);
    .bss : {
        __bss_start = .;
        *(.bss*)
        . = ALIGN(16);
        stack_bottom = .;
        . += 0x100000; /* 256KB stack */
        stack_top = .;
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

