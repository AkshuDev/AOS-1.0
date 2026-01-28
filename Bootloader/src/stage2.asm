BITS 16
ORG 0x10000
STAGE3 equ 0x15000

_start:
    ; Set stack in real mode first
    mov ax, 0x0000
    mov ss, ax
    mov sp, 0x7C00 ; safe low real-mode stack

    ; Enable A20
    call enable_a20

    ; Load gdt_descriptor
    mov al, 0xFF
    out 0xA1, al ; mask slave PIC
    out 0x21, al ; mask master PIC
    cli

    xor ax, ax
    mov ds, ax

    mov eax, ds
    shl eax, 4
    add eax, gdt_start ; Get physical address of the table
    mov [gdt_descriptor + 2], eax ; Patch the linear address field in the descriptor
    lgdt [gdt_descriptor]
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEG:init_pm

[BITS 32]
init_pm: 
    ; set segment registers and stack for protected mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9000

    ; debug marker
    mov dword [magic_pm], 0x504D504D ; 'PMPM'

    ; Clear out the uninitialized memory in the page tables
    mov edi, pml4
    mov ecx, 4096/4
    xor eax, eax
    rep stosd
    mov edi, pdpt
    mov ecx, 4096/4
    rep stosd
    mov edi, pd0
    mov ecx, 4096/4
    rep stosd

    ; ensure paging is off
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; readback CR4 to debug
    mov eax, cr4
    mov [cr4_rb], eax

    lea eax, [pdpt] ; runtime linear address of pdpt (linear==physical now)
    or eax, 3 ; Present | RW
    mov dword [pml4], eax
    mov dword [pml4 + 4], 0 ; high dword = 0

    ; Build PDPT[0] -> PD0
    lea eax, [pd0]
    or eax, 3
    mov dword [pdpt], eax
    mov dword [pdpt + 4], 0

    ; Fill PD0 with 2 MiB identity mappings (PD entries)
    ; PD entry format (64-bit): base (bits 51:12) | flags (low bits)
    ; For 2 MiB pages we use flag 0x83 : Present (1) | RW (2) | PS (0x80)
    mov edi, pd0 ; edi -> PD0 base
    xor ebx, ebx ; ebx = physical base (0, 2MB, 4MB, ...)
    mov ecx, 512 ; 512 entries * 2MiB = 1GiB
.fill_pd_loop:
    mov eax, ebx
    or eax, 0x83 ; set Present|RW|PS
    mov [edi], eax ; low 32 bits
    add edi, 4
    mov dword [edi], 0 ; high 32 bits = 0 (map <4GB)
    add edi, 4
    add ebx, 0x200000 ; next 2MiB
    loop .fill_pd_loop

    ; Load CR3 with physical address of PML4
    lea eax, [pml4]
    mov cr3, eax

    ; readback CR3 into debug var
    mov eax, cr3
    mov [cr3_rb+4], eax

    ; Enable LME (EFER)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) ; set LME
    wrmsr

    ; Load 64-bit GDT runtime
    lgdt [gdt64_descriptor]

    ; Enable paging
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp CODE_SEG64:init_pm64

[BITS 64]
init_pm64:
    ; marker in long mode
    mov dword [magic_pm64], 0x4C4F4E47 ; 'LONG'

    ; set up segments
    mov ax, DATA_SEG64
    mov fs, ax
    mov gs, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x90000
    and rsp, -16
    mov rbp, rsp

    xor rax, rax
    xor rcx, rcx
    xor rdx, rdx
    xor rbx, rbx
    xor rsi, rsi
    xor rdi, rdi
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    mov rax, STAGE3
    jmp rax

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

setup_paging:
    ret

gdt_start:
    dq 0x0000000000000000 ; NULL
    dq 0x00CF9A000000FFFF ; CODE_SEG
    dq 0x00CF92000000FFFF ; DATA_SEG
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt64_descriptor:
    dw gdt64_end - gdt64 - 1
    dq gdt64
gdt64_end:

gdtr_temp:
    dw 0
    dd 0

CODE_SEG equ 0x08
DATA_SEG equ 0x10
CODE_SEG64 equ 0x08
DATA_SEG64 equ 0x10

; Data and debug
ALIGN 16
farptr64: times 6 db 0

cr4_rb: dd 0
cr3_rb: dd 0
efer_rb_lo: dd 0
efer_rb_hi: dd 0

magic_pm: dd 0
magic_pm64: dd 0

section .pagetables
ALIGN 4096
pml4: resb 4096 ; reserve full page for clarity
ALIGN 4096
pdpt: resb 4096
ALIGN 4096
pd0: resb 4096
