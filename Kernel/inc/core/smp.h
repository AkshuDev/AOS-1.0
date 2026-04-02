#pragma once

#include <inttypes.h>
#include <inc/core/kexceptions.h>
#include <inc/core/kfuncs.h>
#include <sys/cdefs.h>

enum thread_status {
    THREAD_STATUS_READY,
    THREAD_STATUS_RUNNING,
    THREAD_STATUS_BLOCKED,
    THREAD_STATUS_DEAD
};

enum core_status {
    CORE_STATUS_READY,
    CORE_STATUS_RESERVED,
    CORE_STATUS_RUNNING
};

struct thread_state {
    void* rsp;

    uint64_t tid;

    void* stack_bottom;
    struct reg_trap_frame* context;

    enum thread_status status;

    struct thread_state* next;
} __attribute__((packed));

struct gdt_tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint32_t reserved4;
    uint16_t io_map_base;
} __attribute__((packed));

struct core_state {
    struct core_state* self;

    uint64_t gdt[7];
    struct tss_entry tss;
    struct gdt_ptr gdt_desc;

    uint32_t lapic_id;
    uint32_t core_idx;

    uint8_t shutdown_core;

    uint8_t reserve_core;
    enum core_status status;

    struct thread_state* cur_thread;
    struct thread_state* idle_thread;
    struct thread_state* ready_list;
    uint64_t next_tid;
    spinlock_t queue_lock;
    spinlock_t command_lock;

    void* stack;
} __attribute__((packed));

void smp_init(void) __attribute__((used));
void smp_push_task(uint32_t core_idx, void (*entry)(void)) __attribute__((used));
void smp_push_task_bsp(void (*entry)(void)) __attribute__((used));
void smp_yield(void) __attribute__((used));

uint8_t smp_get_first_free_core(uint32_t* out) __attribute__((used));
uint8_t smp_get_core_status(uint32_t core_idx, enum core_status* out) __attribute__((used));
void smp_reserve_core(uint32_t core_idx) __attribute__((used));
void smp_unreserve_core(uint32_t core_idx) __attribute__((used));

void smp_timer_handler(void) __attribute__((used));
void smp_ipi_handler(void) __attribute__((used));

void smp_shutdown(void) __attribute__((used));