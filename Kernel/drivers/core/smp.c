#include <aos_inttypes.h>
#include <system.h>

#include <inc/drivers/io/io.h>
#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/core/acpi.h>
#include <inc/core/idt.h>
#include <inc/core/tss_gdt.h>
#include <inc/core/smp.h>

#define LAPIC_REG_ID 0x0020
#define LAPIC_REG_ICR_LOW 0x0300
#define LAPIC_REG_ICR_HIGH 0x0310

#define LAPIC_TMR_INIT_CNT 0x0380
#define LAPIC_TMR_CUR_CNT 0x0390
#define LAPIC_TMR_DIV 0x03E0
#define LAPIC_LVT_TMR 0x0320

#define SMP_IPI_VECTOR 0x40
#define SMP_TLB_IPI_VECTOR 0x41

extern void thread_context_switch(struct thread_state* old, struct thread_state* new);

static uintptr_t lapic_base_virt = 0;
static uint32_t lapic_ticks_per_ms = 0;

static void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic_base_virt + reg) = value;
    (void)*(volatile uint32_t*)(lapic_base_virt + reg);
}

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(lapic_base_virt + reg);
}

static void lapic_timer_calibrate() {
    lapic_write(LAPIC_TMR_DIV, 0x3); 

    lapic_write(LAPIC_TMR_INIT_CNT, 0xFFFFFFFF);
    kdelay(10);
    lapic_write(LAPIC_LVT_TMR, 0x10000); 

    uint32_t ticks_passed = 0xFFFFFFFF - lapic_read(LAPIC_TMR_CUR_CNT);
    lapic_ticks_per_ms = ticks_passed / 10; 
    serial_printf("[SMP : LAPIC] Calibrated: %d ticks/ms\n", lapic_ticks_per_ms);
}

static void lapic_timer_start(uint32_t ms) {
    // set divider to 16
    lapic_write(LAPIC_TMR_DIV, 0x3);

    // Prepare LVT Timer Register:
    // Bits 0-7: Vector (0x30)
    // Bit 17: Periodic Mode (1) or One-shot (0)
    uint32_t vector = 0x30 | (1 << 17); 
    lapic_write(LAPIC_LVT_TMR, vector);

    lapic_write(LAPIC_TMR_INIT_CNT, ms * lapic_ticks_per_ms);
}

static void lapic_init(uintptr_t phys_addr) {
    lapic_base_virt = phys_addr + AOS_DIRECT_MAP_BASE;
	serial_printf("[SMP:LAPIC] Initializing at Virtual Addr 0x%llx (Phys 0x%llx)\n", lapic_base_virt, phys_addr);
    lapic_write(0x80, 0);
    lapic_write(0xF0, 0x1FF);
    lapic_timer_calibrate();
}

static uint8_t get_lapic_id(void) {
    return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}

extern void* smp_trampoline_start;
extern void* smp_trampoline_end;

static spinlock_t boot_lock = 0;
static aos_bool ap_boot_flag = AOS_FALSE;
static uint32_t bsp_core_idx = 0;

static struct core_state* cores[SMP_MAX_CORES] = {0};

static void send_ipi(uint8_t target_apic_id, uint8_t vector) {
    // init IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C500); // ICR Low: INIT
    kdelay(10);

    while (lapic_read(0x300) & (1 << 12)) { __asm__ volatile("pause"); }

    // Startup IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C600 | vector); // ICR Low: STARTUP
}

static void send_wakeup_ipi(uint8_t target_apic_id, uint8_t vector) {
    lapic_write(0x310, (target_apic_id << 24)); // ICR High: Target
    lapic_write(0x300, 0x00004000 | vector); // ICR Low: Fixed, Delivery Mode 000
}

static struct thread_state* create_thread(void (*entry)(void*), void* arg, uint64_t tid) {
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
	*(--stack) = (uintptr_t)arg;   // RDI
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
	thread->arg = arg;

    return thread;
}

static void ap_init_core_state(struct core_state* state) {
    uint32_t low = (uint32_t)((uintptr_t)state);
    uint32_t high = (uintptr_t)state >> 32;
    __asm__ volatile("wrmsr" : : "c"(0xC0000101), "a"(low), "d"(high) : "memory");
}

static void ap_kernel_entry(void) {
	// Enable SSE
	__asm__ volatile("cld");
    uint64_t cr;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr));
    cr &= ~(1 << 2); // Clear EM (Emulation) bit
    cr |= (1 << 1); // Set MP (Monitor Coproccessor) bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr));
    cr = 0;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr));
    cr |= (1 << 9); // Set OSFXSR (FXSAVE/FXRSTOR support)
    cr |= (1 << 10); // Set OSXMMEXCPT (Unmasked Exception support)
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr));

    uint32_t lapic_id = get_lapic_id();
    uint64_t kernel_stack = *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510);
    struct core_state* core = *(struct core_state**)(AOS_DIRECT_MAP_BASE + 0x520);

	if (!gdt_init_ex(&core->gdt, &core->gdt_desc, &core->tss)) {
		__asm__ volatile("cli");
		__asm__ volatile("wbinvd");
		for (;;) { __asm__ volatile("hlt"); }
	}
	if (!tss_init_ex(&core->tss, MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT)) {
		__asm__ volatile("cli");
		__asm__ volatile("wbinvd");
		for (;;) { __asm__ volatile("hlt"); }
	}

    lapic_write(0xF0, 0x1FF); 
    lapic_write(0x80, 0);

    idt_load_local();

    ap_init_core_state(core);

    serial_printf("[SMP] Core %d online!\n", lapic_id);
    core->status = CORE_STATUS_RUNNING;
    core->cur_thread = core->idle_thread;

    lapic_timer_start(10);

    ap_boot_flag = AOS_TRUE;
    while (1) {
        __asm__ volatile("" : : : "memory");
        __asm__ volatile("cli");
        if (core->shutdown_core == 1) {
            break;
        } else if (*(struct thread_state* volatile*)&core->ready_list != NULL) {
            __asm__ volatile("sti");
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
            core->status = core->reserve_core ? CORE_STATUS_RESERVED : CORE_STATUS_READY;
            __asm__ volatile("sti");
            __asm__ volatile("hlt");
            core->status = CORE_STATUS_RUNNING;
        }
    }

    __asm__ volatile("cli");
    __asm__ volatile("wbinvd");
    for (;;) { __asm__ volatile("hlt"); }
}

void smp_ipi_handler(void) {
    lapic_write(0xB0, 0);
}

void smp_tlb_ipi_handler(void) {
	struct core_state* core;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(core));

	if (core->tlb_cmd & SMP_TLB_CMD_INVLPAGE) {
    	uint64_t addr = core->tlb_addr;
		__asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
	} else if (core->tlb_cmd == SMP_TLB_CMD_REFRESH_PAGES) { // THIS COMMAND CANNOT BE USED WITH MULTIPLE COMMANDS
		uint64_t addr = core->tlb_addr;
		__asm__ volatile("mov %0, %%cr3" :: "r"(addr) : "memory");
	}
	__asm__ volatile("mfence" ::: "memory");

    core->tlb_done = AOS_TRUE;
	lapic_write(0xB0, 0);
}

void smp_timer_handler(void) {
    lapic_write(0xB0, 0);
}

void smp_yield(void) {
    struct core_state* core;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(core));

	if (core->core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Yield called by BSP Core? Blocked.\n");
		return;
	}
    
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

void smp_push_task(uint32_t core_idx, void (*entry)(void*), void* arg) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	
	if (core_idx == bsp_core_idx && !smp_is_bsp_core()) {
        serial_printf("[SMP] Warning: AP core tried to push a task to the BSP core! Blocked.\n");
        return;
    }

    struct core_state* target = cores[core_idx];
    struct thread_state* new_thread = create_thread(entry, arg, target->next_tid++);

    spin_lock(&target->queue_lock);
    new_thread->next = target->ready_list;
    target->ready_list = new_thread;
    spin_unlock(&target->queue_lock);

    serial_printf("[SMP] Queueing new task for core %d\n", core_idx);

    if (target->status != CORE_STATUS_RUNNING) {
        serial_printf("[SMP] Sending Awake command for core %d\n", core_idx);
        send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
    }
}

void smp_push_task_bsp(void (*entry)(void*), void* arg) {
	if (!smp_is_bsp_core()) {
        serial_printf("[SMP] Warning: AP core tried to push a task to the BSP core! Blocked.\n");
        return;
    }

    entry(arg);
}

aos_bool smp_get_first_free_core(uint32_t* out) {
    for (uint32_t i = 0; i < SMP_MAX_CORES; i++){
        if (cores[i] == NULL) continue;
		if (i == bsp_core_idx) continue;

        if (cores[i]->status == CORE_STATUS_READY) {
            *out = cores[i]->core_idx;
            return AOS_TRUE;
        }
    }
    return AOS_FALSE;
}

aos_bool smp_get_core_status(uint32_t core_idx, enum core_status *out) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return AOS_FALSE;
    *out = cores[core_idx]->status;
    return AOS_TRUE;
}

aos_bool smp_is_bsp_core(void) {
	struct core_state* core;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(core));

	return core->lapic_id == bsp_core_idx;
}

void smp_reserve_core(uint32_t core_idx) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Reservation called on BSP Core! Blocked.\n");
		return;
	}
	
    struct core_state* target = cores[core_idx];
    uint64_t flags = spin_lock_irqsave(&target->command_lock);

    target->reserve_core = 1;
    if (target->status != CORE_STATUS_RUNNING) {
        serial_printf("[SMP] Sending Awake command for core %d\n", core_idx);
        send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
    }

    spin_unlock_irqrestore(&target->command_lock, flags);
}

void smp_unreserve_core(uint32_t core_idx) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Unreservation called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];
    uint64_t flags = spin_lock_irqsave(&target->command_lock);

    target->reserve_core = 0;
    if (target->status != CORE_STATUS_RUNNING) {
        serial_printf("[SMP] Sending Awake command for core %d\n", core_idx);
        send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
    }

    spin_unlock_irqrestore(&target->command_lock, flags);
}

void smp_init(void) {
    uint8_t apic_ids[SMP_MAX_CORES];
    uint64_t core_count = 0;
    acpi_get_apic_info((uint8_t*)apic_ids, &core_count);

    serial_printf("[SMP] Preparing to wake %lld core...\n", core_count - 1);
    lapic_init(acpi_get_lapic_base());

    uint8_t bsp_apic_id = get_lapic_id();

    uintptr_t trampoline_len = (uintptr_t)&smp_trampoline_end - (uintptr_t)&smp_trampoline_start;
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

    for (uint32_t i = 0; i < (uint32_t)core_count; i++) {
        if (cores[i] != NULL) {
            serial_printf("[SMP] Warning: Core %d already registered, skipping!\n", i);
            continue;
        }

        uint8_t id = apic_ids[i];
        if (id == bsp_apic_id) {bsp_core_idx = i; continue;}

        spin_lock(&boot_lock);
        ap_boot_flag = AOS_FALSE;

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
        ap_core_state->command_lock = 0;
        ap_core_state->stack = (void*)((uintptr_t)ap_stack + 16384);
        ap_core_state->status = CORE_STATUS_READY;
        ap_core_state->next_tid = 0;
        ap_core_state->shutdown_core = 0;
        
        memcpy((void*)(AOS_DIRECT_MAP_BASE + 0x8000), &smp_trampoline_start, trampoline_len);
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x500) = current_cr3;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510) = (uintptr_t)ap_stack + 16384;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x518) = (uintptr_t)ap_kernel_entry;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x520) = (uintptr_t)ap_state;

        cores[i] = ap_core_state;

		__asm__ volatile("mfence" ::: "memory");

        serial_printf("[SMP] Sending SIPI to APIC ID %lld\n", id);
        send_ipi(id, 0x08);

		kdelay_us(200);
		if (!ap_boot_flag) send_ipi(id, 0x08);

		uint64_t timeout = kget_ms_passed();
		while(!ap_boot_flag && kget_ms_passed() - timeout < 1000) { __asm__ volatile("pause");}

        if (!ap_boot_flag) {
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
    lapic_timer_start(10);
    ap_init_core_state(bsp_state);

    __asm__ volatile("sti");
}

uint32_t smp_get_current_core(void) {
	struct core_state* core;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(core));

	return core->core_idx;
}

void smp_shutdown_core(uint32_t core_idx) {
	if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;

	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Shutdown called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];

    uint64_t flags = spin_lock_irqsave(&target->command_lock);
    target->shutdown_core = 1;
	send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
	spin_unlock_irqrestore(&target->command_lock, flags);

	if (target->stack) avmf_free((uint64_t)target->stack);
	if (target->idle_thread) avmf_free((uint64_t)target->idle_thread);
	if (target->ready_list) avmf_free((uint64_t)target->ready_list);

	// Broadcast INIT IPI to reset the AP
	lapic_write(LAPIC_REG_ICR_HIGH, target->lapic_id << 24);
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004500); // INIT, level=assert
	kdelay(10);

	// Deassert INIT3
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004000); // INIT deassert
	kdelay(10);
	
	avmf_free((uint64_t)target);
	cores[core_idx] = NULL;
}

void smp_reset_core(uint32_t core_idx) {
	if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;

	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Reset called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];

    uint64_t flags = spin_lock_irqsave(&target->command_lock);
    target->shutdown_core = 1;
	send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
    spin_unlock_irqrestore(&target->command_lock, flags);

	if (target->ready_list) avmf_free((uint64_t)target->ready_list);

	// Broadcast INIT IPI to reset the AP
	lapic_write(LAPIC_REG_ICR_HIGH, target->lapic_id << 24);
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004500); // INIT, level=assert
	kdelay(10);

	// Deassert INIT3
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004000); // INIT deassert
	kdelay(10);

	spin_lock(&boot_lock);
	ap_boot_flag = AOS_FALSE;

	if (target->idle_thread) {
		target->idle_thread->tid = 0;
		target->idle_thread->status = THREAD_STATUS_RUNNING;
	}
	target->ready_list = NULL;
	target->queue_lock = 0;
	target->command_lock = 0;
	target->status = CORE_STATUS_READY;
	target->next_tid = 0;
	target->shutdown_core = 0;

	uintptr_t trampoline_len = (uintptr_t)&smp_trampoline_end - (uintptr_t)&smp_trampoline_start;
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
	
	memcpy((void*)(AOS_DIRECT_MAP_BASE + 0x8000), &smp_trampoline_start, trampoline_len);
	*(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x500) = current_cr3;
	*(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510) = (uintptr_t)target->stack + 16384;
	*(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x518) = (uintptr_t)ap_kernel_entry;
	*(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x520) = (uintptr_t)target;

	serial_printf("[SMP] Sending SIPI to APIC ID %lld\n", target->lapic_id);
	send_ipi(target->lapic_id, 0x08);
	kdelay_us(200);
	if (!ap_boot_flag) send_ipi(target->lapic_id, 0x08);

	uint64_t timeout = kget_ms_passed();
	while(!ap_boot_flag && kget_ms_passed() - timeout < 1000) { __asm__ volatile("pause");}

	if (!ap_boot_flag) {
		serial_printf("[SMP] Error: Core %lld failed to check in!\n", core_idx);
	} else {
		serial_printf("[SMP] Core %lld checked in successfully.\n", core_idx);
	}

	spin_unlock(&boot_lock);
}

void smp_tlb_core(uint32_t core_idx, uint64_t virt, aos_bool full_flush) {
	if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;

	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: TLB Flush/Invlpage called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];

    uint64_t flags = spin_lock_irqsave(&target->command_lock);

	target->tlb_cmd = full_flush ? SMP_TLB_CMD_REFRESH_PAGES : SMP_TLB_CMD_INVLPAGE;
    target->tlb_addr = virt;
	target->tlb_done = AOS_FALSE;

	serial_printf("[SMP] Sending TLB IPI to APIC ID %lld\n", target->lapic_id);
	send_wakeup_ipi(target->lapic_id, SMP_TLB_IPI_VECTOR);

	uint64_t timeout = kget_ms_passed();
	while(!target->tlb_done && kget_ms_passed() - timeout < 1000) { __asm__ volatile("pause");}

	spin_unlock_irqrestore(&target->command_lock, flags);

	if (kget_ms_passed() - timeout > 1000) {
		serial_printf("[SMP] Error: Core %lld failed to check in on TLB Flush/Invlpage!\n", core_idx);
	} else {
		serial_printf("[SMP] Core %lld checked in successfully on TLB Flush/Invlpage.\n", core_idx);
	}
}

void smp_reset(void) {
	for (int i = 0; i < SMP_MAX_CORES; i++) {
        if (cores[i] == NULL) continue;
		if (i == bsp_core_idx) continue;
        smp_reset_core(i);
    }
}

void smp_shutdown(void) {
    for (int i = 0; i < SMP_MAX_CORES; i++) {
        if (cores[i] == NULL) continue;
		if (i == bsp_core_idx) continue;
        smp_shutdown_core(i);
    }
}

void smp_tlb(uint64_t virt, aos_bool full_flush) {
	for (int i = 0; i < SMP_MAX_CORES; i++) {
        if (cores[i] == NULL) continue;
		if (i == bsp_core_idx) continue;
        smp_tlb_core(i, virt, full_flush);
    }
}
