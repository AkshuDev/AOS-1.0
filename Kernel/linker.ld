ENTRY(kernel_main)

SECTIONS
{
    /* Force the very start of the kernel at VIRTUAL ADDR 0x100000 */
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
		__bss_true_end = .;
        
		. = ALIGN(16);
        stack_bottom__ = .;
        . += 0x10000; /* 64KB stack */
        stack_top__ = .;

		. = ALIGN(16);
		__first_pagemaps = .;
		. += 0x8000;
		__first_pagemaps_end = .;

        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

