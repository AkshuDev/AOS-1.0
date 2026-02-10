[BITS 16]

global smp_trampoline_start
global smp_trampoline_end

smp_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax

    mov eax, 0x8000 + (gdt_start - smp_trampoline_start) ; Code present at 0x8000
    mov [gdt_descriptor - smp_trampoline_start + 0x8000 + 2], eax
    lgdt [gdt_descriptor - smp_trampoline_start + 0x8000]

    ; Enable Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Jump to 32-bit
    jmp 0x08:(prot_mode - smp_trampoline_start + 0x8000)

[BITS 32]
prot_mode:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax

    ; Use the Page Tables provided by the BSP
    mov eax, [0x500] ;Present at 0x500
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable Long Mode (EFER.LME)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable Paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Load the 64-bit GDT
    ; must calculate the physical address of gdt64
    mov eax, 0x8000 + (gdt64 - smp_trampoline_start)
    mov [gdt64_descriptor - smp_trampoline_start + 0x8000 + 2], eax
    lgdt [gdt64_descriptor - smp_trampoline_start + 0x8000]

    ; Jump to 64-bit mode
    jmp 0x08:(long_mode_entry - smp_trampoline_start + 0x8000)

[BITS 64]
long_mode_entry:
    ; Clear segment registers for 64-bit
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load the stack and jump into the Kernel
    mov rsp, [0x510] ; Stack provided at 0x510
    mov rax, [0x518] ; Kernel entry provided at 0x518
    jmp rax

ALIGN 16
gdt_start:
    dq 0x0000000000000000 ; NULL
    dq 0x00CF9A000000FFFF ; 32-bit CODE (0x08)
    dq 0x00CF92000000FFFF ; 32-bit DATA (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0 ; To be patched at runtime

ALIGN 16
gdt64:
    dq 0x0000000000000000 ; NULL
    dq 0x00AF9A000000FFFF ; 64-bit CODE (0x08)
    dq 0x00AF92000000FFFF ; 64-bit DATA (0x10)
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64 - 1
    dq 0 ; To be patched at runtime

smp_trampoline_end:
