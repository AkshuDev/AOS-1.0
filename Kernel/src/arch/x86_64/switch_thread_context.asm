[BITS 64]
global thread_context_switch


; void thread_context_switch(struct thread_state* old, struct thread_state* new)
; RDI = Old Thread, RSI = New Thread
thread_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, [rsi]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

