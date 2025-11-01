#include <inttypes.h>
#include <asm.h>

#include <inc/idt.h>
#include <inc/kexceptions.h>

extern void aos_system_exception_asm(void);

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

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_SIZE - 1;
    idt_ptr.base = (uint64_t)&idt;

    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, (uint64_t)aos_system_exception_asm, 0x08, 0x8E);
    }

    asm volatile("lidt %0" : : "m"(idt_ptr));
    asm volatile ("sti");
}