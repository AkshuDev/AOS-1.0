[BITS 64]
default rel
global aos_system_exception_asm

extern aos_system_exception ; C handler

%macro AOS_SYSTEM_EXCEPTION_ASM_MACRO 1
global aos_system_exception_asm_%1
aos_system_exception_asm_%1:
    %if !(%1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21)
        push 0 ; Error Code
    %endif
    push %1 ; Push the int no.
    
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call aos_system_exception
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp, 16

    iretq
%endmacro

section .text
%assign i 0
%rep 32
    AOS_SYSTEM_EXCEPTION_ASM_MACRO i
    %assign i i+1
%endrep

