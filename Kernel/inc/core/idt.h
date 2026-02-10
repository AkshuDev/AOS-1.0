#pragma once

#include <inttypes.h>
#include <asm.h>

#define IDT_SIZE 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

extern idt_entry_t idt[IDT_SIZE];
extern idt_ptr_t idt_ptr;

void idt_init(void) __attribute__((used));
void idt_load_local(void) __attribute__((used));
void set_idt_entry(int num, uint64_t offset, uint16_t selector, uint8_t type_attr) __attribute__((used));
