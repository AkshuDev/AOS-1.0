; Start of Bootloader
ORG 7C00h                ; Boot sector loads at address 0x7C00
jmp Start                ; Jump to the start of the bootloader

; Set up the Global Descriptor Table (GDT) and selector values
GDTR:
    dw GDTEnd - GDT - 1
    dd GDT

GDT:
    nullsel   equ $ - GDT
    GDT0:
        dd 0
        dd 0
    CodeSel   equ $ - GDT
        dw 0FFFFh
        dw 0
        db 0
        db 09ah
        db 0cfh
        db 0h
    DataSel   equ $ - GDT
        dw 0FFFFh
        dw 0h
        db 0h
        db 092h
        db 0cfh
        db 0
GDTEnd:

; Bootloader starts here
Start:
    ; Debug: print "Starting bootloader"
    mov si, start_msg
    call print_string

    ; Get the drive we booted from
    mov [drive], dl
    ; Update segment registers
    xor ax, ax
    mov ds, ax                ; Set DS to 0

    ; Debug: print "Floppy reset"
    mov si, reset_msg
    call print_string

    ; Reset the floppy drive
    mov ax, 0x00
    mov dl, [drive]
    int 13h                   ; Reset floppy drive
    jc ResetFloppy            ; Retry on failure

    ; Debug: print "Loading Kernel"
    mov si, load_msg
    call print_string

; First floppy read attempt
ReadFloppy:
    mov bx, 9000h             ; Load AOS (Kernel) at 9000h
    mov ah, 0x02
    mov al, 17                ; Load 17 sectors (1 head)
    mov ch, 0                 ; First cylinder
    mov cl, 2                 ; Starting at sector 2 (after bootsector)
    mov dh, 0                 ; First floppy head
    mov dl, [drive]           ; Floppy drive
    int 13h                   ; Read from floppy
    jc ReadFloppy             ; Retry on failure

; Debug: print "Kernel loaded"
    mov si, kernel_loaded_msg
    call print_string

    ; Jump to kernel code (will be loaded at 0x9000)
    jmp dword 9000h            ; Jump to kernel entry point

; Floppy Reset Routine
ResetFloppy:
    ; Debug: print "Floppy reset failed, retrying..."
    mov si, reset_failed_msg
    call print_string
    ; Retry the reset
    mov ax, 0x00
    mov dl, [drive]
    int 13h
    jc ResetFloppy            ; Retry on failure

; End of bootloader

drive db 0

; Debug strings for output
start_msg db 'Starting bootloader...', 0
reset_msg db 'Floppy reset...', 0
load_msg db 'Loading Kernel...', 0
kernel_loaded_msg db 'Kernel loaded successfully!', 0
reset_failed_msg db 'Floppy reset failed, retrying...', 0

; Make sure boot sector is 512 bytes
times 510 - ($ - $$) db 0
dw 0xAA55                   ; Boot signature (0xAA55)

; Subroutine to print a null-terminated string to the screen
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
