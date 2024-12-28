; disk.asm

section .text
    global read_sector      ; Declare the symbol as global so it can be accessed by C

read_sector:
    ; Assuming the parameters are already in the registers:
    ; ah = 0x02 (read sector)
    ; al = number of sectors to read (1)
    ; ch = cylinder number (0)
    ; cl = sector number (1-18)
    ; dh = head number (0)
    ; dl = drive number (0 for A:, 1 for B:)
    ; es:bx = memory address to load the sector to

    int 0x13              ; BIOS interrupt for disk operations
    jc .read_fail         ; Jump if the carry flag is set (error)
    ret                    ; Return if no error

.read_fail:
    ; Handle error (you can customize this part, like printing an error message)
    ret
