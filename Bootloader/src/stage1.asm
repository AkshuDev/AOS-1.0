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

    ; Get EBX and STORE it
    mov ax, 0
    mov es, ax
    mov di, ebx_struct_addr
    xor ebx, ebx
    xor bp, bp

.next_entry:
    mov eax, 0xE820
    mov edx, 0x534D4150 ; 'SMAP'
    mov ecx, 24
    int 0x15

    jc .done_entry
    cmp eax, 0x534D4150 ; 'SMAP'
    jne .done_entry

    test ecx, ecx
    jz .skip_entry

    add di, 24
    inc bp
.skip_entry:
    test ebx, ebx
    jnz .next_entry
.done_entry:
    mov [ebx_info], bp

    mov si, disk_read_msg
    call print_string

    mov ah, 0x42 ; extended load
    mov dl, [disk]
    mov si, dap_s2 ; Load DAP
    int 0x13
    jc disk_error

    mov ah, 0x42
    mov dl, [disk]
    mov si, dap_s3
    int 0x13
    jc disk_error
    
    jmp 0x1000:0x0000 ; Jump to stage 2

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
boot_info equ 0x7FF0
ebx_info equ 0x8000
ebx_struct_addr equ 0x8004

dap_s2:
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 40 ; sectors to read
    dw 0x0000 ; dest offset
    dw 0x1000 ; dest segment
    dq 5 ; starting lba

dap_s3:
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 35 ; sectors to read
    dw 0x0000 ; dest offset
    dw 0x1500 ; dest segment
    dq 45 ; starting lba

times 510 - ($ - $$) db 0
dw 0xAA55 
