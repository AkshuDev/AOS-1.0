ORG 0x7c00
BITS 16

Start:
    mov ax, 0x0003
    int 0x10
    mov si, message
    jmp hang

hang:
    jmp hang

message db "Hi!", 0

times 510 - ($ - $$) db 0
dw 0xAA55