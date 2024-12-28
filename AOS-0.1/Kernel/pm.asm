; pm.asm - Code to switch between Real Mode and Protected Mode

section .data
    ; GDT entries for real mode and protected mode
    gdt_start:
        ; Null descriptor (0x00)
        dd 0x0, 0x0, 0x0, 0x0
        ; Code segment descriptor (0x08) for protected mode (kernel code)
        dd 0x0, 0x9A00, 0xCFFB, 0x0
        ; Data segment descriptor (0x10) for protected mode (kernel data)
        dd 0x0, 0x9200, 0xCFFB, 0x0

    gdt_end:

section .text
    global switch_to_protected, switch_from_protected

; External function to set up the GDT (for switching to protected mode)
setup_gdt:
    ; Load the GDT register with the address of the GDT
    lgdt [gdt_start]
    ret

; Switch to Protected Mode
switch_to_protected:
    ; Step 1: Set up the GDT (Global Descriptor Table)
    call setup_gdt

    ; Step 2: Set the PE (Protection Enable) bit in the EFLAGS register
    mov eax, cr0
    or eax, 1              ; Set PE bit (bit 0) to enable Protected Mode
    mov cr0, eax

    ; Step 3: Far jump to flush the pipeline and switch to protected mode
    jmp 0x08:pm_protected_mode

; Code to execute after switching to protected mode
pm_protected_mode:
    ; Set up a new stack for protected mode if necessary
    ; (Optional: set up a new stack for protected mode if you're doing stack switching)
    ; This example doesn't need to switch the stack for simplicity

    ; The CPU is now in protected mode, return here
    ret

; Switch from Protected Mode back to Real Mode
switch_from_protected:
    ; Step 1: Clear the PE bit in CR0 to disable Protected Mode
    mov eax, cr0
    and eax, 0xFFFFFFFE      ; Clear PE bit (bit 0)
    mov cr0, eax

    ; Step 2: Far jump to reset to real mode
    jmp 0x08:real_mode_start

; Code to execute after switching back to Real Mode
real_mode_start:
    ; Set up a real-mode stack if necessary
    ; Optional: reinitialize segments and stack here

    ; CPU is now back in real mode, return here
    ret
