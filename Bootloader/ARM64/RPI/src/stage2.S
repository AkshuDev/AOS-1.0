.section .text
    .extern stage3
    .global stage2

.align 4
stage2:
    ldr x0, =vector_table
    msr vbar_el1, x0
    isb

    bl setup_page_tables
    bl enable_mmu

    bl stage3

    // Failsafe
    bl disable_mmu
    msr vbar_el1, #0
    msr daifset, #0xf
    ret // stage 1 decides what to do

setup_page_tables:
    ldr x0, =l1_table
    ldr x1, =l0_table
    orr x0, x0, #0b11 // valid + table
    str x0, [x1]

    ldr x1, =l1_table
    ldr x0, =#0x441 // 0b00000000000000000010001000001
    str x0, [x1]

    ret

enable_mmu:
    msr sctlr_el1, #1
    ret

disable_mmu:
    msr sctlr_el1, #0
    ret

.section .data
    .align 16
    vector_table: .byte 0, 0, 0, 0, 0, 0, 0, 0

.section .bss
    .align 12
    l0_table: .skip 0x1000
    l1_table: .skip 0x1000
    l2_table: .skip 0x1000
    l3_table: .skip 0x1000