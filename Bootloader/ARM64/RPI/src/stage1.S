.section .text
    .extern stage2
    .global _start

_start:
    ldr x0, =stack_top
    mov sp, x0

    msr daifset, #0xf
    bl stage2
    b . // infinite loop

.section .bss
    stack: .skip 0x10000
    stack_top: .skip 0x0