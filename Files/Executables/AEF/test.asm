section .text
    global WinMain

section .data
    msg db "Hello World!", 0xa, 0xd
    len equ $ - msg

WinMain:
    mov ecx, msg
    mov edx, len
    call Write
    call Exit

Write:
    mov eax, 4
    mov ebx, 1
    mov ecx, ecx
    mov edx, edx
    syscall

Exit:
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    mov eax, 1
    syscall