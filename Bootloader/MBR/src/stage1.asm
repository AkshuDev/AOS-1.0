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

    ; Setup VGA 80x25
    mov ah, 0x0 ; Ask for VGA
    mov al, 0x3 ; Mode=3 (80x25 TEXT, 16 colors, 8 pages)
    int 0x10
    jc video_mode_setup_error

    mov ah, 0x2 ; Set Cursor
    mov bh, 0x0 ; Default Page
    mov dh, 0x0 ; Row = 0
    mov dl, 0x0 ; Col = 0
    int 0x10
    jc video_set_cursor_error

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

	mov cx, 218
load_stage3_loop:
    cmp cx, 127
    jle final_chunk

    mov word [dap_s3_1+2],127
    call read_dap_s3

   	add dword [dap_s3_1+8],127
    add word [dap_s3_1+6],127*32 ; advance segment
    sub cx,127

    jmp load_stage3_loop
final_chunk:
    mov [dap_s3_1+2],cx
    call read_dap_s3

    mov ah, 0x42 ; extended load
    mov dl, [disk]
    mov si, dap_ambrc ; Load DAP
    int 0x13
    jc disk_error
    
    jmp 0x1000:0x0000 ; Jump to stage 2

disk_error:
    mov si, disk_err_msg
    call print_string

    hlt

video_mode_setup_error:
    mov si, video_mode_setup_errormsg
    call print_string
    
    hlt

video_set_cursor_error:
    mov si, video_set_cursor_errormsg
    call print_string

    hlt

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

read_dap_s3:
	mov ah, 0x42 ; extended load
    mov dl, [disk]
    mov si, dap_s3_1 ; Load DAP
    int 0x13
    jc disk_error
	ret

disk db 0x80 ; Default 0x80
disk_read_msg db "Reading and Loading Stage2...", 0xa, 0xd, 0
disk_err_msg db "Error Reading Disk!", 0xa, 0xd, 0
video_mode_setup_errormsg db "Failed to Setup VGA 80x25 TEXT Mode!", 0xa, 0xd, 0
video_set_cursor_errormsg db "Failed to set cursor position to (0, 0)!", 0xa, 0xd, 0
boot_info equ 0x7FF0
ebx_info equ 0x8000
ebx_struct_addr equ 0x8004

dap_s2:
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 32 ; sectors to read
    dw 0x0000 ; dest offset
    dw 0x1000 ; dest segment
    dq 1024 ; starting lba

dap_s3_1:
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 127 ; sectors to read
    dw 0x0000 ; dest offset
    dw 0x1500 ; dest segment
    dq 2048 ; starting lba

dap_ambrc: ; AOS Master Boot Record Config
    db 0x10 ; size (16 bytes)
    db 0 ; reserved
    dw 2 ; sectors to read
    dw 0x0500 ; dest offset
    dw 0x0000 ; dest segment
    dq 2046 ; starting lba

times 512 - ($ - $$) db 0 ; PBFS CLI will take care of the rest!
