#pragma once

#include <aos_inttypes.h>
#include <inc/mm/avmf.h>
typedef struct {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;

    uint16_t reserved3;

    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid1;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_mid2;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct {
    gdt_entry_t null;

    gdt_entry_t kernel_code;
    gdt_entry_t kernel_data;

    gdt_entry_t user_data;
    gdt_entry_t user_code;

    gdt_tss_entry_t tss;
} __attribute__((packed)) gdt_t;

void gdt_init(void) __attribute__((used));
void tss_init(void) __attribute__((used));
aos_bool gdt_init_ex(gdt_t* gdt, gdtr_t* gdtr, tss_t* tss) __attribute__((used));
aos_bool tss_init_ex(tss_t* tss, MemoryAllocType mtype, uint32_t flags) __attribute__((used));