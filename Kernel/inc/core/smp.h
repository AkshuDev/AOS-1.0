#pragma once

#include <inttypes.h>
#include <inc/core/kexceptions.h>
#include <inc/core/kfuncs.h>

enum thread_status {
    THREAD_STATUS_READY,
    THREAD_STATUS_RUNNING,
    THREAD_STATUS_BLOCKED,
    THREAD_STATUS_DEAD
};

enum core_status {
    CORE_STATUS_READY,
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

struct core_state {
    struct core_state* self;

    uint32_t lapic_id;
    uint32_t core_idx;

    enum core_status status;

    struct thread_state* cur_thread;
    struct thread_state* idle_thread;
    struct thread_state* ready_list;
    uint64_t next_tid;
    spinlock_t queue_lock;

    void* stack;
} __attribute__((packed));

void smp_init(void) __attribute__((used));
void smp_push_task(uint32_t core_idx, void (*entry)(void)) __attribute__((used));
void smp_push_task_bsp(void (*entry)(void)) __attribute__((used));
void smp_yield(void) __attribute__((used));

void smp_ipi_handler(void) __attribute__((used));
