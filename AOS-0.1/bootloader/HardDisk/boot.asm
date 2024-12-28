; Start Of Assembly Code:

; The BIOS loads this at 7C00h, so tell the assembler ( NASM ) that that's where it's loaded.
	ORG 7c00h

; Now that NASM knows where this is loaded, we jump to the main bootsector code:
	jmp Start      ; Goto Start

; Start
Start:
	mov [ drive ],		dl        ; Get the HardDisk AOS booted from

	; Update the segment registers
	xor ax,		ax		; XOR ax
	mov ds,		ax		; Mov AX into DS

	jmp Running      ; Go to the running part of the bootsector

; This is where the boot program runs.
Running:

; Clear text mode screen
	mov ax,		3		; Set text-mode 3.
	int 10h				; Text-mode 3 set. The screen is cleared.

; Load the AOS C Kernel.

; Reset the HardDisk drive. Need to do this to read the C kernel.
ResetHardDisk:
	mov ax,		0x00		; Select HardDisk Reset BIOS Function
        mov dl,		[ drive ]	; Select the HardDisk AOS booted from

        int 13h				; Reset the HardDisk drive

        jc ResetHardDisk		; If there was a error, try again.

; Read the HardDisk drive for loading the AOS C+ Kernel
ReadHardDisk:
         mov bx,	9000h		; Load AOS at 9000h.
         mov ah,	0x02		; Load disk data to ES:BX
         mov al,	17		; Load two HardDisk head full's worth of data.

         mov ch,	0		; First Cylinder
         mov cl,	2		; Start at the 2nd Sector, so you don't load the bootsector, you load
					;  the C Kernel/Second Stage Loader ( they are linked together ).
         mov dh,	0		; Use first HardDisk head
         mov dl,	[ drive ]	; Load from the drive AOS booted from.

         int 13h			; Read the HardDisk disk.

	 jc ReadHardDisk			; Error, try again.

; Now, since AOS is over 17 sectors ( one HardDisk head has 17 sectors ( 18, sector 0 is included in the
;  number ), we load another HardDisk head.
ReadHardDisk2:
	mov al,		17	; The Second Head Full
	inc dh			; Set it to the second head

	int 13h			; Read the HardDisk disk.

	jc ReadHardDisk2		; If there was a error, try again.

; Get ready to set PMode ( Protected Mode)
SetPMode:
        cli			; Stop BIOS interrups - Protected Mode can't handle interrupts without a
				;  PMode IDT. The BIOS loads a Real-Mode IDT, and Protected Mode has different
				;   exceptions and other things. So AOS can't handle interrupts and IRQ's
				;    currently.

        ; Load a temporary GDTR. You need a GDT to set up the PMode selectors, and to allow the C++ kernel's
	;  stack to function properly.
	lgdt [ GDTR ]		; Load the GDTR.

        ; Set Protected Mode ( PMode )
        mov eax,	cr0
        or eax,		1
        mov cr0,	eax		; PMode bit set.

	; Here we are now in Protected Mode

	; BUT, we need to do this to make the CPU set the registers correctly:
        jmp dword CodeSel:PMode

; We are in protected mode here:
[bits 32]
PMode:
        ; Put the Data Selector into eax for setting up other registers.
        mov eax,	DataSel		; Data Selector.

	; Make SS, DS, ES, FS and GS = the Data Selector ( In the GDT )
        mov ss, eax
        mov ds, eax
        mov es, eax
        mov fs, eax
        mov gs, eax

	; Set up the PMode stack:
        mov ax, 0x10
        mov ss, ax
        mov esp, 0xFFFF ; To Fix...AOS will work correctly, all that I need to fix is make the stack
			; larger.

	; Stop the HardDisk motor from spinning ( since we are in PMode, the BIOS can't stop it so we stop it )

        mov dl,		[ drive ]	; Select which motor to stop ( the HardDisk drive AOS booted from )

        jmp dword CodeSel:9000h	; Jump to the C Kernel. The C Kernel is loaded at 9000h. So go there.

; The global descriptor table this tells the computer where all the segments ( now selectors ) are.
; This is just a temporary GDT. We will load a better one in the 2nd stage kernel.
GDTR:
    dw GDTEnd-1
      dd GDT
GDT:
nullsel equ $-GDT
GDT0:
          dd 0
          dd 0
CodeSel equ $-GDT
          dw 0ffffh
          dw 0
          db 0
          db 09ah
          db 0cfh
          db 0h
DataSel equ $-GDT
        dw 0ffffh
        dw 0h
        db 0h
        db 092h
        db 0cfh
        db 0
GDTEnd:

; This part makes sure the bootsector is 512 bytes.

times 509-($-$$) db 0	; 509 bytes because drive takes 1 byte and 0xAA55 takes 2, so correctly set the value
                ;  Otherwise, the bootsector will be too big or small, if that happens, AOS will
                ;   not work correctly.

; This tells the BIOS to load the bootsector, ALWAYS need this. The BIOS sees this as a signal that this is
;  a bootable disk.
dw 0xAA55

; For storing which HardDisk drive AOS booted from
drive db 0

; End of boot.asm
