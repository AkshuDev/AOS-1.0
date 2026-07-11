#pragma once

#include <aos_inttypes.h>
#include <inc/core/kexceptions.h>
#include <inc/core/kfuncs.h>
#include <inc/core/tss_gdt.h>

#define SMP_MAX_CORES 256

#define SMP_TLB_CMD_INVLPAGE (1 << 0)
#define SMP_TLB_CMD_REFRESH_PAGES (1 << 1)

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
	void* arg;

    uint64_t tid;

    void* stack_bottom;
    struct reg_trap_frame* context;

    enum thread_status status;

    struct thread_state* next;
} __attribute__((packed));

struct core_state {
    struct core_state* self;

    gdt_t gdt;
    tss_t tss;
    gdtr_t gdt_desc;

	uint32_t tlb_cmd;
	uint64_t tlb_addr;
	aos_bool tlb_done;

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
void smp_push_task(uint32_t core_idx, void (*entry)(void*), void* arg) __attribute__((used));
void smp_push_task_bsp(void (*entry)(void*), void* arg) __attribute__((used));
void smp_yield(void) __attribute__((used));

aos_bool smp_get_first_free_core(uint32_t* out) __attribute__((used));
aos_bool smp_get_core_status(uint32_t core_idx, enum core_status* out) __attribute__((used));
void smp_reserve_core(uint32_t core_idx) __attribute__((used));
void smp_unreserve_core(uint32_t core_idx) __attribute__((used));

void smp_timer_handler(void) __attribute__((used));
void smp_ipi_handler(void) __attribute__((used));

aos_bool smp_is_bsp_core(void) __attribute__((used));
uint32_t smp_get_current_core(void) __attribute__((used));

void smp_shutdown_core(uint32_t core_idx) __attribute__((used));
void smp_reset_core(uint32_t core_idx) __attribute__((used));
void smp_tlb_core(uint32_t core_idx, uint64_t virt, aos_bool full_flush) __attribute__((used));
void smp_reset(void) __attribute__((used));
void smp_shutdown(void) __attribute__((used));
void smp_tlb(uint64_t virt, aos_bool full_flush) __attribute__((used));