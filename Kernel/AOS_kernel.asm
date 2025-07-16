BITS 16
ORG 0x1000

%define ENDL 0xa

section .text
    global _start

    ; GDT Structure
    gdt:
        dq 0x0000000000000000    ; Null descriptor
        dq 0x00CF9A000000FFFF    ; Code segment descriptor
        dq 0x00CF92000000FFFF    ; Data segment descriptor

    gdt_descriptor:
        dw gdt_descriptor - gdt - 1  ; Size of GDT
        dd gdt                      ; Address of GDT

    CODE_SEG equ 0x08
    DATA_SEG equ 0x10

_start:
    ; Switch to protected mode
    mov si, kernel_msg
    call print_string_real

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_protected

init_protected:
    ; Set up segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Read partition table to get FAT32 partition start
    mov eax, 0        ; LBA 0 (MBR sector)
    mov edi, partition_table
    call read_sector
    mov eax, [partition_table + 8] ; Partition start LBA
    mov [partition_start], eax

    ; Load BPB from the first sector of the FAT32 partition
    add eax, 0        ; BPB is at first sector of partition
    mov edi, bpb
    call read_sector

    ; Read FSInfo sector from partition_start + 1
    mov eax, [partition_start]
    add eax, 1        ; FSInfo is at the second sector
    mov edi, fsinfo_buffer
    call read_sector

    ; Print confirmation message
    call print_success

    ; Halt
    cli
    hlt

; ------------------------------------------------
; Read Sector using LBA (FAT32 optimized)
; ------------------------------------------------
read_sector:
    push eax
    push edi

    ; Send LBA sector number (eax) to ATA controller
    mov dx, 0x1F2  ; Sector Count Register
    mov al, 1      ; Read 1 sector
    out dx, al

    mov dx, 0x1F3  ; LBA low byte
    out dx, al

    mov dx, 0x1F4  ; LBA mid byte
    out dx, al

    mov dx, 0x1F5  ; LBA high byte
    out dx, al

    mov dx, 0x1F7  ; Command Register
    mov al, 0x20   ; READ SECTORS command
    out dx, al

.wait:
    in al, dx      ; Read Status Register
    test al, 0x08  ; Check if DRQ (Data Request) is set
    jz .wait

    mov dx, 0x1F0  ; Data Register
    mov ecx, 256   ; 256 words (512 bytes)

.read_loop:
    in ax, dx
    stosw         ; Store word in buffer (edi)
    loop .read_loop

    pop edi
    pop eax
    ret

; ------------------------------------------------
; Print String using VGA Text Mode
; ------------------------------------------------
print_string:
    push esi
    mov edi, 0xB8000  ; VGA Text Mode memory
    mov ah, 0x07      ; White text on black background

.print_loop:
    lodsb             ; Load next byte from string
    test al, al      ; Check for NULL terminator
    jz .done
    cmp al, ENDL     ; Check for newline
    je .new_line

    stosw            ; Store character and attribute in VGA memory
    jmp .print_loop

.new_line:
    add edi, 160     ; Move to next line
    jmp .print_loop

.done:
    pop esi
    ret

; ------------------------------------------------
; Print Success Message
; ------------------------------------------------
print_success:
    lea esi, [success_msg]
    call print_string
    ret

; ------------------------------------------------
; Prints a string in REAL MODE (BIOS)
; ------------------------------------------------
print_string_real:
    pusha               ; Save registers
    mov ah, 0x0E        ; BIOS teletype function

.next_char:
    lodsb               ; Load character from [SI] into AL
    test al, al         ; Check for null terminator
    jz .done            ; If null, stop

    int 0x10            ; Print character
    jmp .next_char      ; Repeat

.done:
    popa                ; Restore registers
    ret

; ------------------------------------------------
; Data Buffers
; ------------------------------------------------
bpb                times 512 db 0
fsinfo_buffer      times 512 db 0
partition_table    times 512 db 0
partition_start    dd 0
success_msg       db "FAT32 Loaded Successfully!", ENDL, 0
kernel_msg db "AOS Kernel Loaded!", ENDL, 0

; Segments
CODE_SEG equ 0x08
DATA_SEG equ 0x10