#include <inttypes.h>
#include <system.h>

#include <inc/drivers/io/io.h>
#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/core/acpi.h>
#include <inc/core/idt.h>
#include <inc/core/smp.h>

#define LAPIC_REG_ID 0x0020
#define LAPIC_REG_ICR_LOW 0x0300
#define LAPIC_REG_ICR_HIGH 0x0310

extern void thread_context_switch(struct thread_state* old, struct thread_state* new);

static uintptr_t lapic_base_virt = 0;

static void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic_base_virt + reg) = value;
    (void)*(volatile uint32_t*)(lapic_base_virt + reg);
}

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(lapic_base_virt + reg);
}

static void lapic_init(uintptr_t phys_addr) {
    lapic_base_virt = phys_addr + AOS_DIRECT_MAP_BASE;
    lapic_write(0x80, 0);
    lapic_write(0xF0, 0x1FF);
}

static uint8_t get_lapic_id(void) {
    return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}

static void mdelay(uint64_t ms) {
    for (int i = 0; i < 0xFF + ms; i++) {
        asm volatile("pause"); // TODO: Add HPET based timer in kfuncs, and use it here!
    }
}

extern void* smp_trampoline_start;
extern void* smp_trampoline_end;

static spinlock_t boot_lock = 0;
static uint8_t ap_boot_flag = 0;
static uint32_t bsp_core_idx = 0;

static struct core_state* cores[256] = {0};

static void send_ipi(uint8_t target_apic_id, uint8_t vector) {
    // init IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C500); // ICR Low: INIT
    mdelay(10);

    while (lapic_read(0x300) & (1 << 12)) { asm("pause"); }

    // Startup IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C600 | vector); // ICR Low: STARTUP
}

static void send_wakeup_ipi(uint8_t target_apic_id, uint8_t vector) {
    lapic_write(0x310, (target_apic_id << 24)); // ICR High: Target
    lapic_write(0x300, 0x00004000 | vector); // ICR Low: Fixed, Delivery Mode 000
}

static struct thread_state* create_thread(void (*entry)(void), uint64_t tid) {
    uint64_t thread_virt = (uint64_t)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
    if (thread_virt == NULL) 
        return NULL;
    struct thread_state* thread = (struct thread_state*)thread_virt;
    
    uint64_t stack_virt = (uint64_t)avmf_alloc(16384, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
    if (stack_virt == NULL)
        return NULL;
    void* stack_raw = (void*)stack_virt;
    uint64_t* stack = (uint64_t*)((uint8_t*)stack_raw + 16384);

    *(--stack) = (uintptr_t)entry; // RIP
    *(--stack) = 0; // RBP
    *(--stack) = 0; // RBX
    *(--stack) = 0; // R12
    *(--stack) = 0; // R13
    *(--stack) = 0; // R14
    *(--stack) = 0; // R15

    thread->rsp = stack;
    thread->tid = tid;
    thread->stack_bottom = stack_raw;
    thread->status = THREAD_STATUS_READY;

    return thread;
}

static uint8_t ap_init_core_state(struct core_state* state) {
    uint32_t low = (uint32_t)((uintptr_t)state);
    uint32_t high = (uintptr_t)state >> 32;
    asm volatile("wrmsr" : : "c"(0xC0000101), "a"(low), "d"(high) : "memory");
}

static void ap_kernel_entry(void) {
    uint32_t lapic_id = get_lapic_id();
    uint64_t kernel_stack = *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510);

    idt_load_local();

    struct core_state* core = *(struct core_state**)(AOS_DIRECT_MAP_BASE + 0x520);
    ap_init_core_state(core);

    serial_printf("[SMP] Core %d online!\n", lapic_id);
    core->status = CORE_STATUS_RUNNING;
    core->cur_thread = core->idle_thread;

    asm volatile("sti");
    ap_boot_flag = 1;
    while (1) {
        asm volatile("" : : : "memory");
        if (*(struct thread_state* volatile*)&core->ready_list != NULL) {
            spin_lock(&core->queue_lock);
            struct thread_state* next = core->ready_list;
            core->ready_list = next->next;
            spin_unlock(&core->queue_lock);

            struct thread_state* prev = core->cur_thread;
            core->cur_thread = next;
            next->status = THREAD_STATUS_RUNNING;

            serial_printf("[SMP : CORE %d] Found Task!\n", core->core_idx);

            thread_context_switch(prev, next);
        } else {
            core->status = CORE_STATUS_READY;
            asm volatile("hlt");
            serial_printf("[SMP : CORE %d] Rewaken!\n", core->core_idx);
            core->status = CORE_STATUS_RUNNING;
        }
    }
}

void smp_ipi_handler(void) {
    serial_print("[SMP] Waking up core...\n");
    lapic_write(0xB0, 0);
}

void smp_yield(void) {
    struct core_state* core;
    asm("mov %%gs:0, %0" : "=r"(core));
    
    struct thread_state* cur = core->cur_thread;
    struct thread_state* idle = core->idle_thread;
    
    if (cur == idle) return;
    cur->status = THREAD_STATUS_READY;
    spin_lock(&core->queue_lock);
    cur->next = core->ready_list;
    core->ready_list = cur;
    spin_unlock(&core->queue_lock);

    core->cur_thread = idle;
    thread_context_switch(cur, idle);
}

void smp_push_task(uint32_t core_idx, void (*entry)(void)) {
    if (core_idx > 255 || cores[core_idx] == NULL) return;

    struct core_state* target = cores[core_idx];
    struct thread_state* new_thread = create_thread(entry, target->next_tid++);

    spin_lock(&target->queue_lock);
    new_thread->next = target->ready_list;
    target->ready_list = new_thread;
    spin_unlock(&target->queue_lock);

    serial_printf("[SMP] Queueing new task for core %d\n", core_idx);

    if (target->status != CORE_STATUS_RUNNING) {
        serial_printf("[SMP] Sending Awake command for core %d\n", core_idx);
        send_wakeup_ipi(target->lapic_id, 0x40);
    }
}

void smp_push_task_bsp(void (*entry)(void)) {
    uint32_t core_idx = bsp_core_idx;
    if (core_idx > 255 || cores[core_idx] == NULL) return;

    struct core_state* target = cores[core_idx];
    struct thread_state* new_thread = create_thread(entry, target->next_tid++);

    spin_lock(&target->queue_lock);
    new_thread->next = target->ready_list;
    target->ready_list = new_thread;
    spin_unlock(&target->queue_lock);

    if (target->status != CORE_STATUS_RUNNING) {
        send_wakeup_ipi(target->lapic_id, 0x40);
    }
}

void smp_init(void) {
    uint8_t apic_ids[256];
    uint64_t core_count = 0;
    acpi_get_apic_info((uint8_t*)apic_ids, &core_count);

    serial_printf("[SMP] Preparing to wake %lld core...\n", core_count - 1);
    lapic_init(acpi_get_lapic_base());

    uint8_t bsp_apic_id = get_lapic_id();

    uintptr_t trampoline_len = (uintptr_t)&smp_trampoline_end - (uintptr_t)&smp_trampoline_start;
    memcpy((void*)(AOS_DIRECT_MAP_BASE + 0x8000), &smp_trampoline_start, trampoline_len);
    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x500) = current_cr3;

    for (uint32_t i = 0; i < (uint32_t)core_count; i++) {
        uint8_t id = apic_ids[i];
        if (id == bsp_apic_id) {bsp_core_idx = i; continue;}

        spin_lock(&boot_lock);
        ap_boot_flag = 0;

        void* ap_stack = (void*)avmf_alloc(16384, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
        if (!ap_stack) {
            serial_printf("[SMP] Error: Could not allocate stack for core %lld\n", id);
            spin_unlock(&boot_lock);
            continue;
        }
        void* ap_state = (void*)avmf_alloc(sizeof(struct core_state), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
        if (!ap_state) {
            serial_printf("[SMP] Error: Could not allocate state structure for core %lld\n", id);
            spin_unlock(&boot_lock);
            continue;
        }
        struct core_state* ap_core_state = (struct core_state*)ap_state;
        uint64_t idle_thread_virt = (uint64_t)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
        if (idle_thread_virt == 0) {
            serial_printf("[SMP] Error: Could not allocate idle thread state structure for core %lld\n", id);
            spin_unlock(&boot_lock);
            continue;
        }

        ap_core_state->self = ap_core_state;
        ap_core_state->lapic_id = id;
        ap_core_state->core_idx = i;
        ap_core_state->idle_thread = (struct thread_state*)idle_thread_virt;
        ap_core_state->idle_thread->tid = 0;
        ap_core_state->idle_thread->status = THREAD_STATUS_RUNNING;
        ap_core_state->ready_list = NULL;
        ap_core_state->queue_lock = 0;
        ap_core_state->stack = (void*)((uintptr_t)ap_stack + 16384);
        ap_core_state->status = CORE_STATUS_READY;
        ap_core_state->next_tid = 0;

        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510) = (uintptr_t)ap_stack + 16384;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x518) = (uintptr_t)ap_kernel_entry;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x520) = (uintptr_t)ap_state;

        cores[i] = ap_core_state;

        serial_printf("[SMP] Sending SIPI to APIC ID %lld\n", id);
        send_ipi(id, 0x08);
        // TODO: Add a second ip to be sent after 200ms incase core doesn't respond

        uint64_t timeout = 0xFFFFFF;
        while(ap_boot_flag == 0 && timeout != 0) { asm volatile("pause"); timeout--; }

        if (ap_boot_flag == 0) {
            serial_printf("[SMP] Error: Core %lld failed to check in!\n", id);
        } else {
            serial_printf("[SMP] Core %lld checked in successfully.\n", id);
        }

        spin_unlock(&boot_lock);
    }

    void* bsp_state_virt = (void*)avmf_alloc(sizeof(struct core_state), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
    if (!bsp_state_virt) return;
    struct core_state* bsp_state = (struct core_state*)bsp_state_virt;
    bsp_state->self = bsp_state;
    bsp_state->lapic_id = bsp_apic_id;
    bsp_state->core_idx = bsp_core_idx;
    bsp_state->ready_list = NULL;
    bsp_state->queue_lock = 0;
    bsp_state->status = CORE_STATUS_RUNNING;
    bsp_state->next_tid = 0;

    void* idle_thread_virt = (void*)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
    if (idle_thread_virt) {
        struct thread_state* idle_thread = (struct thread_state*)idle_thread_virt;
        idle_thread->tid = 0;
        idle_thread->status = THREAD_STATUS_RUNNING;
        bsp_state->idle_thread = idle_thread;
        bsp_state->cur_thread = idle_thread;
    } else {
        bsp_state->idle_thread = NULL;
        bsp_state->cur_thread = NULL;
    }

    cores[bsp_core_idx] = bsp_state;

    ap_init_core_state(bsp_state);
}
