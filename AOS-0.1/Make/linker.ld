ENTRY(_start)

SECTIONS {
    /* Set start of the kernel at 0x1000000 */
    . = 0x1000000;

    .text : {
        *(.text)    /* All code will be placed here */
    }

    .data : {
        *(.data)    /* All initialized data will go here */
    }

    .bss : {
        *(.bss)     /* All uninitialized data will go here */
    }

    .reloc : {
        *(.reloc)   /* Relocation sections */
    }

    /* Ensure that we don't place sections below 0x1000000 */
    . = 0x1000000; /* Ensures that the sections are loaded at or above 0x1000000 */
}
