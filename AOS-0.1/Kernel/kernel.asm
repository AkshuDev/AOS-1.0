[bits 32]              ; 32-bit mode for Protected Mode

section .data
    mode_info:
        times 256 db 0     ; Allocate 256 bytes for mode information block

    vesa_failed_ db "VESA Failed!", 0

section .text
    global _start
    extern switch_to_protected
    extern switch_from_protected
;jmp _start             ; Jump to start of the program


_start:
    call switch_from_protected
    ; Step 1: Set video mode using VESA function 0x4F02 (Set Mode)
    mov eax, 0x4F02     ; VESA Set Mode function
    mov ebx, 0x118      ; Mode 0x118: 1024x768 with 32-bit color (4 bytes per pixel)
    int 0x10            ; Call BIOS VESA interrupt
    jc vesa_failed      ; If carry flag is set, mode setting failed

    ; Step 2: Query VESA Mode Information (using function 0x4F01)
    mov eax, 0x4F01     ; VESA Get Mode Info function
    mov ebx, 0x118      ; Mode 0x118 (1024x768 32-bit color)
    lea esi, [mode_info] ; Load the address of the mode info buffer into ESI
    int 0x10            ; Call BIOS VESA interrupt
    jc vesa_failed      ; If carry flag is set, the query failed

    ; Step 3: Extract framebuffer address and bytes per row
    ; The information returned by 0x4F01 is stored in the buffer at mode_info
    ; Framebuffer address is at offset 0x0A
    mov esi, [mode_info + 0x0A]  ; Framebuffer address (32-bit)
    ; Bytes per row is at offset 0x12 (number of pixels * bytes per pixel)
    mov edi, [mode_info + 0x12]  ; Stride (bytes per row)

    ; Step 4: Set up square parameters
    mov edx, 10         ; Height of the square (10 pixels)
    mov ecx, 10         ; Width of the square (10 pixels)
    mov ebx, 0xADD8E6   ; Color of the square (light blue in BGR format)

    ; Step 5: Draw the square in the framebuffer
    jmp outer_loop
    call switch_to_protected
    jmp hang

outer_loop:
    ; Move to the correct starting position of the row (stride used here)
    add esi, edi        ; Move by the stride for the current row
    mov edi, ecx        ; Width (number of pixels in a row)

    ; Jump to inner_loop to start drawing the current row
    jmp inner_loop      ; Jump to inner_loop to draw the current row

inner_loop:
    mov [esi], ebx      ; Store the color at the current pixel location
    add esi, 4          ; Move to the next pixel (4 bytes per pixel)
    dec edi             ; Decrement the column counter
    jnz inner_loop      ; Continue for all pixels in the row

    ; Move to the next row
    ; Compute the number of bytes to skip to the next row (1024 - width) * 4
    mov eax, 1024       ; Load 1024 (width of screen in pixels)
    sub eax, ecx        ; Subtract the width of the square
    shl eax, 2          ; Multiply by 4 (since each pixel is 4 bytes)
    add esi, eax        ; Add the computed byte offset to the framebuffer address
    dec edx             ; Decrement the height counter
    jnz outer_loop      ; Continue for all rows

hang:
    jmp hang            ; Infinite loop to "hang" the system

vesa_failed:
    ; If VESA mode failed, hang the system
    mov si, [vesa_failed_]
    call print_string
    call switch_to_protected
    jmp hang

print_string:
    mov ah, 0x0E
.next_char:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next_char
.done:
    ret