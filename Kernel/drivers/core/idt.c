#include <inttypes.h>
#include <asm.h>

#include <inc/core/idt.h>
#include <inc/core/kexceptions.h>

extern void aos_system_exception_asm_0(void);
extern void aos_system_exception_asm_1(void);
extern void aos_system_exception_asm_2(void);
extern void aos_system_exception_asm_3(void);
extern void aos_system_exception_asm_4(void);
extern void aos_system_exception_asm_5(void);
extern void aos_system_exception_asm_6(void);
extern void aos_system_exception_asm_7(void);
extern void aos_system_exception_asm_8(void);
extern void aos_system_exception_asm_9(void);
extern void aos_system_exception_asm_10(void);
extern void aos_system_exception_asm_11(void);
extern void aos_system_exception_asm_12(void);
extern void aos_system_exception_asm_13(void);
extern void aos_system_exception_asm_14(void);
extern void aos_system_exception_asm_15(void);
extern void aos_system_exception_asm_16(void);
extern void aos_system_exception_asm_17(void);
extern void aos_system_exception_asm_18(void);
extern void aos_system_exception_asm_19(void);
extern void aos_system_exception_asm_20(void);
extern void aos_system_exception_asm_21(void);
extern void aos_system_exception_asm_22(void);
extern void aos_system_exception_asm_23(void);
extern void aos_system_exception_asm_24(void);
extern void aos_system_exception_asm_25(void);
extern void aos_system_exception_asm_26(void);
extern void aos_system_exception_asm_27(void);
extern void aos_system_exception_asm_28(void);
extern void aos_system_exception_asm_29(void);
extern void aos_system_exception_asm_30(void);
extern void aos_system_exception_asm_31(void);

extern void aos_int_smp_ipi(void);

idt_entry_t idt[IDT_SIZE];
idt_ptr_t idt_ptr;

void set_idt_entry(int num, uint64_t offset, uint16_t selector, uint8_t type_attr) {
    idt[num].offset_low = offset & 0xFFFF;
    idt[num].offset_mid = (offset >> 16) & 0xFFFF;
    idt[num].offset_high = (offset >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = type_attr;
    idt[num].ist = 0;
}

void idt_load_local(void) {
    asm volatile("lidt %0" : : "m"(idt_ptr));
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_SIZE - 1;
    idt_ptr.base = (uint64_t)&idt;

    set_idt_entry(0, (uint64_t)aos_system_exception_asm_0, 0x08, 0x8E);
    set_idt_entry(1, (uint64_t)aos_system_exception_asm_1, 0x08, 0x8E);
    set_idt_entry(2, (uint64_t)aos_system_exception_asm_2, 0x08, 0x8E);
    set_idt_entry(3, (uint64_t)aos_system_exception_asm_3, 0x08, 0x8E);
    set_idt_entry(4, (uint64_t)aos_system_exception_asm_4, 0x08, 0x8E);
    set_idt_entry(5, (uint64_t)aos_system_exception_asm_5, 0x08, 0x8E);
    set_idt_entry(6, (uint64_t)aos_system_exception_asm_6, 0x08, 0x8E);
    set_idt_entry(7, (uint64_t)aos_system_exception_asm_7, 0x08, 0x8E);
    set_idt_entry(8, (uint64_t)aos_system_exception_asm_8, 0x08, 0x8E);
    set_idt_entry(9, (uint64_t)aos_system_exception_asm_9, 0x08, 0x8E);
    set_idt_entry(10, (uint64_t)aos_system_exception_asm_10, 0x08, 0x8E);
    set_idt_entry(11, (uint64_t)aos_system_exception_asm_11, 0x08, 0x8E);
    set_idt_entry(12, (uint64_t)aos_system_exception_asm_12, 0x08, 0x8E);
    set_idt_entry(13, (uint64_t)aos_system_exception_asm_13, 0x08, 0x8E);
    set_idt_entry(14, (uint64_t)aos_system_exception_asm_14, 0x08, 0x8E);
    set_idt_entry(15, (uint64_t)aos_system_exception_asm_15, 0x08, 0x8E);
    set_idt_entry(16, (uint64_t)aos_system_exception_asm_16, 0x08, 0x8E);
    set_idt_entry(17, (uint64_t)aos_system_exception_asm_17, 0x08, 0x8E);
    set_idt_entry(18, (uint64_t)aos_system_exception_asm_18, 0x08, 0x8E);
    set_idt_entry(19, (uint64_t)aos_system_exception_asm_19, 0x08, 0x8E);
    set_idt_entry(20, (uint64_t)aos_system_exception_asm_20, 0x08, 0x8E);
    set_idt_entry(21, (uint64_t)aos_system_exception_asm_21, 0x08, 0x8E);
    set_idt_entry(22, (uint64_t)aos_system_exception_asm_22, 0x08, 0x8E);
    set_idt_entry(23, (uint64_t)aos_system_exception_asm_23, 0x08, 0x8E);
    set_idt_entry(24, (uint64_t)aos_system_exception_asm_24, 0x08, 0x8E);
    set_idt_entry(25, (uint64_t)aos_system_exception_asm_25, 0x08, 0x8E);
    set_idt_entry(26, (uint64_t)aos_system_exception_asm_26, 0x08, 0x8E);
    set_idt_entry(27, (uint64_t)aos_system_exception_asm_27, 0x08, 0x8E);
    set_idt_entry(28, (uint64_t)aos_system_exception_asm_28, 0x08, 0x8E);
    set_idt_entry(29, (uint64_t)aos_system_exception_asm_29, 0x08, 0x8E);
    set_idt_entry(30, (uint64_t)aos_system_exception_asm_30, 0x08, 0x8E);
    set_idt_entry(31, (uint64_t)aos_system_exception_asm_31, 0x08, 0x8E);

    set_idt_entry(0x40, (uint64_t)aos_int_smp_ipi, 0x08, 0x8E);

    asm volatile("lidt %0" : : "m"(idt_ptr));
    asm volatile ("sti");
}
