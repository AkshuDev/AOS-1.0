ENTRY(kernel_main)

SECTIONS
{
    /* Force the very start of the kernel at 0x100000 */
    . = 0x100000;

    /* Kernel entry section */
    .start : {
        KEEP(*(.start))       /* kernel_main goes here */
    }

    /* All other code, including PBFS helpers */
    .text : {
        *(.text*)             /* everything else goes after .start */
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss : {
        __bss_start = .;
        *(.bss*)
        . = ALIGN(16);
        stack_bottom = .;
        . += 0x4000; /* 16KB stack */
        stack_top = .;
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

