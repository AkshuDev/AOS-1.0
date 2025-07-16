BITS 64

%include "../headers/aos.inc"

section .text
    global WinMain

    ;io console
    global print_8
    global print_16
    global print_32
    global print_64
    global read_8
    global read_16
    global read_32
    global read_64
    ;io files
    global write_file
    global read_file
    global read_file64
    global write_file64
    ;extra
    global _xor_
    global sum
    global subtract
    global multiply
    global divide
    ;exit
    global exit_

WinMain:
    ret

write_file:
    ;PARAMS:
    ;ECX: FILE NAME
    ;EDX: MODE - 0644 (rw-r--r--)
    ;EDI: Data
    ;ESI: FLAGS - 0x201 (O_WRONLY | O_CREAT | O_TRUNC)
    ;ESP: Data Size

    ;RETURNS:
    ;NONE

    ;Open the file
    mov eax, 5
    mov ebx, ecx
    mov ecx, esi
    ;USE EDX: 0644
    mov edx, edx
    syscall
    mov ebx, eax

    ;Write
    mov eax, 4
    mov ecx, edi
    mov edx, esp
    syscall

    ;Close
    mov eax, 6
    syscall

read_file:
    ;PARAMS:
    ;ECX: FILE NAME
    ;EDX: MODE - 0 (not required)
    ;EDI: BUFFER
    ;ESI: FLAGS - 0 (O_RDONLY)
    ;ESP: Data Size

    ;RETURNS:
    ;RCX: DATA

    ;Open the file
    mov eax, 5
    mov ebx, ecx
    mov ecx, esi
    mov edx, edx
    syscall

    mov ebx, eax

    ;Read
    mov eax, 3
    mov ecx, edi
    mov edx, esp
    syscall

    ;Close
    mov eax, 6
    syscall

    ret

write_file64:
    ;PARAMS:
    ;RCX: FILE NAME
    ;RDX: MODE - 0644 (rw-r--r--)
    ;RDI: Data
    ;RSI: FLAGS - 0x201 (O_WRONLY | O_CREAT | O_TRUNC)
    ;RSP: Data Size

    ;RETURNS:
    ;NONE

    ;Open the file
    mov rax, 5
    mov rbx, rcx
    mov rcx, rsi
    ;USE EDX: 0644
    mov rdx, rdx
    syscall
    mov rbx, rax

    ;Write
    mov rax, 4
    mov rcx, rdi
    mov rdx, rsp
    syscall

    ;Close
    mov rax, 6
    syscall

read_file64:
    ;PARAMS:
    ;RCX: FILE NAME
    ;RDX: MODE - 0 (not required)
    ;RDI: BUFFER
    ;RSI: FLAGS - 0 (O_RDONLY)
    ;RSP: Data Size

    ;RETURNS:
    ;RCX: DATA

    ;Open the file
    mov rax, 5
    mov rbx, rcx
    mov rcx, rsi
    mov rdx, rdx
    syscall

    mov rbx, rax

    ;Read
    mov rax, 3
    mov rcx, rdi
    mov rdx, rsp
    syscall

    ;Close
    mov rax, 6
    syscall

    mov rax, 0

    ret

print_8:
    mov al, 4
    mov bl, 1
    mov cl, cl
    mov dl, dl
    syscall
    ret

print_16:
    mov ax, 4
    mov bx, 1
    mov cx, cx
    mov dx, dx
    syscall
    ret

print_32:
    mov eax, 4
    mov ebx, 1
    mov ecx, ecx
    mov edx, edx
    syscall
    ret

print_64:
    mov rax, 4
    mov rbx, 1
    mov rcx, rcx
    mov rdx, rdx
    syscall
    ret

read_8:
    mov al, 3
    mov bl, 0
    mov cl, cl
    mov dl, dl
    syscall
    ret

read_16:
    mov ax, 3
    mov bx, 0
    mov cx, cx
    mov dx, dx
    syscall
    ret

read_32:
    mov eax, 3
    mov ebx, 0
    mov ecx, ecx
    mov edx, edx
    syscall
    ret

read_64:
    mov rax, 3
    mov rbx, 0
    mov rcx, rcx
    mov rdx, rdx
    syscall
    ret

_xor_:
    mov eax, ecx
    mov ebx, edx
    xor ecx, ecx
    xor edx, edx
    xor eax, ebx
    ret

sum:
    mov eax, ecx
    mov ebx, edx
    xor ecx, ecx
    xor edx, edx
    add eax, ebx
    ret

subtract:
    mov eax, ecx
    mov ebx, edx
    xor ecx, ecx
    xor edx, edx
    sub eax, ebx
    ret

multiply:
    mov eax, ecx
    mov ebx, edx
    xor ecx, ecx
    xor edx, edx
    mul eax
    ret

divide:
    mov eax, ecx
    mov ebx, edx
    xor ecx, ecx
    xor edx, edx
    div eax
    ret

exit_:
    mov eax, 1
    xor edx, edx
    xor ebx, ebx
    xor ecx, ecx
    syscall
