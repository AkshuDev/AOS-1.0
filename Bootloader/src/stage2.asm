BITS 16
GLOBAL _start
EXTERN stage3

_start:
    ; Set stack in real mode first
    mov ax, 0x0000
    mov ss, ax
    mov sp, 0x7C00 ; safe low real-mode stack

    ; Enable A20
    call enable_a20

    ; Load gdt_descriptor
    cli
    lgdt [gdt_descriptor]
    lidt [idtr]
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov gs, ax
    mov fs, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x9000

    call stage3

hang:
    hlt
    jmp hang

enable_a20:
.wait_input:
    in   al, 0x64 ; read status
    test al, 2
    jnz  .wait_input ; wait until input buffer empty
    mov  al, 0xD1
    out  0x64, al
.wait_input2:
    in   al, 0x64
    test al, 2
    jnz  .wait_input2
    mov  al, 0xDF ; set A20 bit
    out  0x60, al
    ret

gdt_start:
    dq 0x0000000000000000 ; NULL
    dq 0x00CF9A000000FFFF ; CODE_SEG
    dq 0x00CF92000000FFFF ; DATA_SEG
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

idt_start:
    dq 0 ; one null entry
idt_end:

idtr:
    dw idt_end - idt_start - 1 ; limit = size - 1
    dd idt_start ; base

CODE_SEG equ 0x08
DATA_SEG equ 0x10
