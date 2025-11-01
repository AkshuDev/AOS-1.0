[BITS 32]
default rel
global aos_system_exception_asm

extern aos_system_exception ; C handler

section .text
aos_system_exception_asm:
    pushad ; save registers

    ; Save stack
    push esp
    call aos_system_exception
    add esp, 4 ; pop argument

    popad ; restore registers
    add esp, 0 ; error code (adjustment needed)
    iret ; return from int
