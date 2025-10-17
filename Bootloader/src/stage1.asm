[BITS 16]
[ORG 0x7C00]

start:
    jmp 0x0000:.setup_segments
.setup_segments:
    ; Clean every register
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Setup stack pointer
    mov sp, start
    cld ; Clear directional flag

    mov [disk], dl ; Get DISK
    mov ah, 0
    mov al, dl
    mov [boot_info], al ; Save it some known place

    mov si, disk_read_msg
    call print_string

    mov ah, 0x42 ; extended load
    mov dl, [disk]
    mov si, dap ; Load DAP
    int 0x13
    jc disk_error
    
    jmp 0x0100:0x0000 ; Jump to stage 2

disk_error:
    mov si, disk_err_msg
    call print_string

    hlt

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    ; Also do on COM1
    mov dx, 0x3F8
    out dx, al
    jmp print_string
.done:
    ret


disk db 0x80 ; Default 0x80
disk_read_msg db "Reading and Loading Stage2...", 0xa, 0xd, 0
disk_err_msg db "Error Reading Disk!", 0xa, 0xd, 0
boot_info equ 0x8FF0

dap:
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 15 ; sectors to read
    dw 0x0000 ; dest offset
    dw 0x0100 ; dest segment
    dq 5 ; starting lba

times 510 - ($ - $$) db 0
dw 0xAA55 
