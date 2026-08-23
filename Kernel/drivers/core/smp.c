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
static uint32_t bsp_apic_id = 0;

static struct core_state* cores[SMP_MAX_CORES] = {0};

static void send_ipi(uint8_t target_apic_id, uint8_t vector) {
    // init IPI
    lapic_write(LAPIC_REG_ICR_HIGH, (target_apic_id << 24)); // ICR High
    lapic_write(LAPIC_REG_ICR_LOW, 0x0000C500); // ICR Low: INIT

    while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12)) { __asm__ volatile("pause"); }

    // Startup IPI
    lapic_write(LAPIC_REG_ICR_HIGH, (target_apic_id << 24)); // ICR High
    lapic_write(LAPIC_REG_ICR_LOW, 0x0000C600 | vector); // ICR Low: STARTUP

	while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12)) { __asm__ volatile("pause"); }
}

static void send_wakeup_ipi(uint8_t target_apic_id, uint8_t vector) {
    lapic_write(0x310, (target_apic_id << 24)); // ICR High: Target
    lapic_write(0x300, 0x00004000 | vector); // ICR Low: Fixed, Delivery Mode 000
}

static struct thread_state* create_thread(void (*entry)(void*), void* arg) {
    uint64_t thread_virt = (uint64_t)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
    if (thread_virt == NULL) return NULL;
    struct thread_state* thread = (struct thread_state*)thread_virt;
	memset(thread, 0, sizeof(struct thread_state));
    
    uint64_t stack_virt = (uint64_t)avmf_alloc(PAGE_SIZE, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
    if (stack_virt == NULL) return NULL;
    void* stack_raw = (void*)stack_virt;
	memset(stack_raw, 0, PAGE_SIZE);

    uint64_t* stack = (uint64_t*)((uint8_t*)stack_raw + PAGE_SIZE);

    *(--stack) = (uintptr_t)entry; // RIP
	*(--stack) = (uintptr_t)arg;   // RDI
    *(--stack) = 0; // RBP
    *(--stack) = 0; // RBX
    *(--stack) = 0; // R12
    *(--stack) = 0; // R13
    *(--stack) = 0; // R14
    *(--stack) = 0; // R15

    thread->rsp = stack;
    thread->stack_bottom = stack_raw;
	thread->stack_size = PAGE_SIZE;
	thread->arg = arg;
    thread->status = THREAD_STATUS_READY;

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
	if (!tss_init_ex(&core->tss, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW)) {
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

	enum core_status idle_status = CORE_STATUS_READY;

    ap_boot_flag = AOS_TRUE;
    while (1) {
        __asm__ volatile("" : : : "memory");
        __asm__ volatile("cli");

		uint64_t cmd_rflags = spin_lock_irqsave(&core->command_lock);
        if (core->command != 0) {
			aos_bool shutdown = AOS_FALSE;

            switch (core->command) {
				case SMP_CMD_SHUTDOWN: {
					shutdown = AOS_TRUE;
					core->response = SMP_RESP_ACK_SHUTDOWN;
					break;
				}
				case SMP_CMD_RESERVE: {
					idle_status = CORE_STATUS_RESERVED;
					core->response = SMP_RESP_ACK_RESERVE;
					break;
				}
				case SMP_CMD_UNRESERVE: {
					idle_status = CORE_STATUS_READY;
					core->response = SMP_RESP_ACK_UNRESERVE;
					break;
				}
				default: {
					core->response = SMP_RESP_INV_CMD;
					break;
				}
			}

			if (shutdown) {
				spin_unlock_irqrestore(&core->command_lock, cmd_rflags);	
				break;
			}
        }
		spin_unlock_irqrestore(&core->command_lock, cmd_rflags);
		
		if (*(volatile struct thread_state**)&core->ready_list != NULL) {
            __asm__ volatile("sti");

			uint64_t rflags = spin_lock_irqsave(&core->queue_lock);
			struct thread_state* next = core->ready_list;
			spin_unlock_irqrestore(&core->queue_lock, rflags);
			
			while (next && next->status != THREAD_STATUS_READY) {
				if (next) {
					uint64_t rflags_thread = spin_lock_irqsave(&next->thread_lock);
					struct thread_state* nxt = next->next;
					spin_unlock_irqrestore(&next->thread_lock, rflags_thread);

					next = nxt;
				}
			}
			if (!next) goto wait_for_task;

			rflags = spin_lock_irqsave(&core->queue_lock);
			core->ready_list = next->next;
			if (core->ready_list_end == next) core->ready_list_end = next->prev;
			spin_unlock_irqrestore(&core->queue_lock, rflags);

			rflags = spin_lock_irqsave(&next->thread_lock);
            next->status = THREAD_STATUS_RUNNING;
			spin_unlock_irqrestore(&next->thread_lock, rflags);

            serial_printf("[SMP : CORE %d] Found Task\n", core->core_idx);
			rflags = spin_lock_irqsave(&core->queue_lock);

			struct thread_state* cur = core->cur_thread;
			core->cur_thread = next;

			spin_unlock_irqrestore(&core->queue_lock, rflags);
            thread_context_switch(cur, next);
        } else {
			wait_for_task: {
				core->status = idle_status;
				__asm__ volatile("sti");
				__asm__ volatile("hlt");
				core->status = CORE_STATUS_RUNNING;
			}
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

	core->tlb_resp = 0;
	if (core->tlb_cmd & SMP_TLB_CMD_INVLPAGE) {
    	uint64_t addr = core->tlb_addr;
		__asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");

		core->tlb_resp |= SMP_TLB_RESP_ACK_INVPAGE;
	} else if (core->tlb_cmd == SMP_TLB_CMD_REFRESH_PAGES) { // THIS COMMAND CANNOT BE USED WITH MULTIPLE COMMANDS
		uint64_t addr = core->tlb_addr;
		__asm__ volatile("mov %0, %%cr3" :: "r"(addr) : "memory");

		core->tlb_resp |= SMP_TLB_RESP_ACK_REFRESH_PAGES;
	}
	core->tlb_cmd = 0;
	__asm__ volatile("mfence" ::: "memory");

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

	// Set Thread as Dead
	uint64_t rflags = spin_lock_irqsave(&cur->thread_lock);
	cur->status = THREAD_STATUS_DEAD;
	cur->next = NULL;
	spin_unlock_irqrestore(&cur->thread_lock, rflags);

	rflags = spin_lock_irqsave(&core->queue_lock);

	if (core->finished_list_end) core->finished_list_end->next = cur;
	cur->prev = core->finished_list_end;

	core->finished_list_end = cur;
	if (!core->finished_list) core->finished_list = cur;

	core->cur_thread = idle;
	spin_unlock_irqrestore(&core->queue_lock, rflags);

    thread_context_switch(cur, idle);
}

void smp_push_task(uint32_t core_idx, void (*entry)(void*), void* arg) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	
	if (core_idx == bsp_core_idx && !smp_is_bsp_core()) {
        serial_printf("[SMP] Warning: AP core tried to push a task to the BSP core! Blocked.\n");
        return;
    }

    struct core_state* target = cores[core_idx];
	struct thread_state* new_thread = NULL;

	uint64_t rflags = spin_lock_irqsave(&target->queue_lock);
	if (target->finished_list) {
		struct thread_state* prev_t = NULL;
		struct thread_state* cur_t = target->finished_list;

		if (cur_t) {
			uint64_t rflags_thread = spin_lock_irqsave(&cur_t->thread_lock);
			spin_unlock_irqrestore(&target->queue_lock, rflags);

			aos_bool first_loop = AOS_TRUE;
			while (cur_t) {
				if (!first_loop) rflags_thread = spin_lock_irqsave(&cur_t->thread_lock);
				else first_loop = AOS_FALSE;

				if (cur_t->status == THREAD_STATUS_DEAD) {
					if (cur_t->stack_size < 1024) {
						// Such a thread cannot be ever used so we free it
						struct thread_state* inv_thread = cur_t;

						// Unlink Invalid Thread
						if (prev_t) {
							uint64_t rflags_prev_thread = spin_lock_irqsave(&prev_t->thread_lock);
							prev_t->next = cur_t->next;
							spin_unlock_irqrestore(&prev_t->thread_lock, rflags_prev_thread);
						}
						if (cur_t->next) {
							uint64_t rflags_nxt_thread = spin_lock_irqsave(&cur_t->next->thread_lock);
							cur_t->next->prev = prev_t;
							spin_unlock_irqrestore(&cur_t->next->thread_lock, rflags_nxt_thread);
						}
						
						rflags = spin_lock_irqsave(&target->queue_lock);
						if (target->finished_list == cur_t) target->finished_list = cur_t->next;
						if (target->finished_list_end == cur_t) target->finished_list_end = cur_t->prev;
						spin_unlock_irqrestore(&target->queue_lock, rflags);

						cur_t = cur_t->next;

						if (inv_thread->stack_bottom) avmf_free((uint64_t)inv_thread->stack_bottom);
						inv_thread->stack_bottom = NULL;
						inv_thread->stack_size = 0;
						inv_thread->rsp = NULL;

						spin_unlock_irqrestore(&inv_thread->thread_lock, rflags_thread);
						avmf_free((uint64_t)inv_thread);
						continue;
					}

					new_thread = cur_t;

					// Unlink New Thread
					if (prev_t) {
						uint64_t rflags_prev_thread = spin_lock_irqsave(&prev_t->thread_lock);
						prev_t->next = cur_t->next;
						spin_unlock_irqrestore(&prev_t->thread_lock, rflags_prev_thread);
					}
					if (cur_t->next) {
						uint64_t rflags_nxt_thread = spin_lock_irqsave(&cur_t->next->thread_lock);
						cur_t->next->prev = prev_t;
						spin_unlock_irqrestore(&cur_t->next->thread_lock, rflags_nxt_thread);
					}

					spin_unlock_irqrestore(&cur_t->thread_lock, rflags_thread);
					rflags = spin_lock_irqsave(&target->queue_lock);

					if (target->finished_list == cur_t) target->finished_list = cur_t->next;
					if (target->finished_list_end == cur_t) target->finished_list_end = cur_t->prev;

					spin_unlock_irqrestore(&target->queue_lock, rflags);
					rflags_thread = spin_lock_irqsave(&new_thread->thread_lock);

					// Set thread
					new_thread->rsp = new_thread->stack_bottom + new_thread->stack_size; // Reset RSP

					uint64_t* stack = (uint64_t*)new_thread->rsp;
					*(--stack) = (uintptr_t)entry; // RIP
					*(--stack) = (uintptr_t)arg; // RDI
					*(--stack) = 0; // RBP
					*(--stack) = 0; // RBX
					*(--stack) = 0; // R12
					*(--stack) = 0; // R13
					*(--stack) = 0; // R14
					*(--stack) = 0; // R15

					spin_unlock_irqrestore(&new_thread->thread_lock, rflags_thread);
					break;
				}

				continue_search: {
					prev_t = cur_t;

					struct thread_state* nxt = cur_t->next;
					spin_unlock_irqrestore(&cur_t->thread_lock, rflags_thread);
					cur_t = nxt;
				}
			}
		} else spin_unlock_irqrestore(&target->queue_lock, rflags);
	}
	if (!new_thread) new_thread = create_thread(entry, arg);

	uint64_t rflags_thread = spin_lock_irqsave(&new_thread->thread_lock);
	new_thread->arg = arg;
    new_thread->status = THREAD_STATUS_READY;
	spin_unlock_irqrestore(&new_thread->thread_lock, rflags_thread);

	rflags = spin_lock_irqsave(&target->queue_lock);

	if (target->ready_list_end) target->ready_list_end->next = new_thread;
	new_thread->prev = target->ready_list_end;

    target->ready_list_end = new_thread;
	if (!target->ready_list) target->ready_list = new_thread;

    spin_unlock_irqrestore(&target->queue_lock, rflags);

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

	return core->lapic_id == bsp_apic_id;
}

void smp_reserve_core(uint32_t core_idx) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Reservation called on BSP Core! Blocked.\n");
		return;
	}
	
    struct core_state* target = cores[core_idx];
    uint64_t rflags = spin_lock_irqsave(&target->command_lock);
    target->command = SMP_CMD_RESERVE;
	target->response = 0;
	if (target->status != CORE_STATUS_RUNNING) send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
	spin_unlock_irqrestore(&target->command_lock, rflags);

	while (target->response == 0) {
		if (target->response == SMP_RESP_ACK_RESERVE) break;
		else if (target->response == SMP_RESP_INV_CMD) {
			return; // Faliure to shutdown, highly highly highly unlikely
		}
		__asm__ volatile("pause");
	}
}

void smp_unreserve_core(uint32_t core_idx) {
    if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;
	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: Unreservation called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];
	
    uint64_t rflags = spin_lock_irqsave(&target->command_lock);
    target->command = SMP_CMD_UNRESERVE;
	target->response = 0;
	if (target->status != CORE_STATUS_RUNNING) send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
	spin_unlock_irqrestore(&target->command_lock, rflags);

	while (target->response == 0) {
		if (target->response == SMP_RESP_ACK_UNRESERVE) break;
		else if (target->response == SMP_RESP_INV_CMD) {
			return; // Faliure to shutdown, highly highly highly unlikely
		}
		__asm__ volatile("pause");
	}
}

void smp_init(void) {
    uint8_t apic_ids[SMP_MAX_CORES];
    uint64_t core_count = 0;
    acpi_get_apic_info((uint8_t*)apic_ids, &core_count);

    serial_printf("[SMP] Preparing to wake %lld core...\n", core_count - 1);
    lapic_init(acpi_get_lapic_base());

    bsp_apic_id = get_lapic_id();

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

        uint64_t rflags_core = spin_lock_irqsave(&boot_lock);
        ap_boot_flag = AOS_FALSE;

        void* ap_stack = (void*)avmf_alloc(PAGE_SIZE*4, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
        if (!ap_stack) {
            serial_printf("[SMP] Error: Could not allocate stack for core %lld\n", id);
            spin_unlock_irqrestore(&boot_lock, rflags_core);
            continue;
        }
        memset(ap_stack, 0, PAGE_SIZE*4);

		void* ap_state = (void*)avmf_alloc(sizeof(struct core_state), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
        if (!ap_state) {
            serial_printf("[SMP] Error: Could not allocate state structure for core %lld\n", id);
            spin_unlock_irqrestore(&boot_lock, rflags_core);
            continue;
        }
        memset(ap_state, 0, sizeof(struct core_state));
		
		struct core_state* ap_core_state = (struct core_state*)ap_state;
        uint64_t idle_thread_virt = (uint64_t)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
        if (idle_thread_virt == 0) {
            serial_printf("[SMP] Error: Could not allocate idle thread state structure for core %lld\n", id);
            spin_unlock_irqrestore(&boot_lock, rflags_core);
            continue;
        }
		memset((void*)idle_thread_virt, 0, sizeof(struct thread_state));

        ap_core_state->self = ap_core_state;
        ap_core_state->lapic_id = id;
        ap_core_state->core_idx = i;
        ap_core_state->idle_thread = (struct thread_state*)idle_thread_virt;
        ap_core_state->idle_thread->status = THREAD_STATUS_RUNNING;
        ap_core_state->ready_list = NULL;
        ap_core_state->queue_lock = 0;
        ap_core_state->command_lock = 0;
        ap_core_state->stack = (void*)((uintptr_t)ap_stack + 16384);
        ap_core_state->status = CORE_STATUS_READY;
        ap_core_state->command = 0;
        
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

        spin_unlock_irqrestore(&boot_lock, rflags_core);
    }

    void* bsp_state_virt = (void*)avmf_alloc(sizeof(struct core_state), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
    if (!bsp_state_virt) return;
    struct core_state* bsp_state = (struct core_state*)bsp_state_virt;
	memset(bsp_state, 0, sizeof(struct core_state));

    bsp_state->self = bsp_state;
    bsp_state->lapic_id = bsp_apic_id;
    bsp_state->core_idx = bsp_core_idx;
    bsp_state->ready_list = NULL;
    bsp_state->queue_lock = 0;
    bsp_state->status = CORE_STATUS_RUNNING;
	bsp_state->command = 0;

    void* idle_thread_virt = (void*)avmf_alloc(sizeof(struct thread_state), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
    if (idle_thread_virt) {
        struct thread_state* idle_thread = (struct thread_state*)idle_thread_virt;
		memset(idle_thread, 0, sizeof(struct thread_state));

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

    uint64_t rflags = spin_lock_irqsave(&target->command_lock);
    target->command = SMP_CMD_SHUTDOWN;
	target->response = 0;
	if (target->status != CORE_STATUS_RUNNING) send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
	spin_unlock_irqrestore(&target->command_lock, rflags);

	uint64_t timeout = kget_ms_passed();
	while (target->response == 0 && kget_ms_passed() - timeout < 10000) {
		if (target->response == SMP_RESP_ACK_SHUTDOWN) break;
		else if (target->response == SMP_RESP_INV_CMD) {
			return; // Faliure to shutdown, highly highly highly unlikely
		}
		__asm__ volatile("pause");
	}
	if (kget_ms_passed() - timeout > 1000) return; // Faliure to shutdown

	rflags = spin_lock_irqsave(&target->queue_lock);

	if (target->ready_list) {
		uint64_t rflags_thread = 0;
		struct thread_state* cur = target->ready_list;

		if (cur) rflags_thread = spin_lock_irqsave(&cur->thread_lock);

		target->ready_list = NULL;
		target->ready_list_end = NULL;
		spin_unlock_irqrestore(&target->queue_lock, rflags);

		aos_bool first_loop = AOS_TRUE;
		while (cur) {
			if (first_loop) first_loop = AOS_FALSE;
			else rflags_thread = spin_lock_irqsave(&cur->thread_lock);

			if (cur->stack_bottom) avmf_free((uint64_t)cur->stack_bottom);
			cur->stack_bottom = NULL;
			cur->rsp = NULL;
			cur->stack_size = 0;
			cur->status = THREAD_STATUS_BLOCKED; // Dead threads can be reused, blocked threads cannot be reused, and are skipped entirely
			cur->prev = NULL;

			struct thread_state* nxt = cur->next;
			cur->next = NULL;

			spin_unlock_irqrestore(&cur->thread_lock, rflags_thread);
			avmf_free((uint64_t)cur);

			cur = nxt;
		}

		rflags = spin_lock_irqsave(&target->queue_lock);
	} else target->ready_list_end = NULL;

	if (target->finished_list) {
		uint64_t rflags_thread = 0;
		struct thread_state* cur = target->finished_list;

		if (cur) rflags_thread = spin_lock_irqsave(&cur->thread_lock);

		target->finished_list = NULL;
		target->finished_list_end = NULL;
		spin_unlock_irqrestore(&target->queue_lock, rflags);

		aos_bool first_loop = AOS_TRUE;
		while (cur) {
			if (first_loop) first_loop = AOS_FALSE;
			else rflags_thread = spin_lock_irqsave(&cur->thread_lock);

			if (cur->stack_bottom) avmf_free((uint64_t)cur->stack_bottom);
			cur->stack_bottom = NULL;
			cur->rsp = NULL;
			cur->stack_size = 0;
			cur->status = THREAD_STATUS_BLOCKED; // Dead threads can be reused, blocked threads cannot be reused, and are skipped entirely
			cur->prev = NULL;

			struct thread_state* nxt = cur->next;
			cur->next = NULL;

			spin_unlock_irqrestore(&cur->thread_lock, rflags_thread);
			avmf_free((uint64_t)cur);

			cur = nxt;
		}

		rflags = spin_lock_irqsave(&target->queue_lock);
	} else target->finished_list_end = NULL;

	if (target->idle_thread) avmf_free((uint64_t)target->idle_thread);
	target->idle_thread = NULL;

	if (target->stack) avmf_free((uint64_t)target->stack);
	target->stack = NULL;

	spin_unlock_irqrestore(&target->queue_lock, rflags);

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

    uint64_t rflags = spin_lock_irqsave(&target->command_lock);
    target->command = SMP_CMD_SHUTDOWN;
	target->response = 0;
	if (target->status != CORE_STATUS_RUNNING) send_wakeup_ipi(target->lapic_id, SMP_IPI_VECTOR);
	spin_unlock_irqrestore(&target->command_lock, rflags);

	uint64_t timeout = kget_ms_passed();
	while (target->response == 0 && kget_ms_passed() - timeout < 10000) {
		if (target->response == SMP_RESP_ACK_SHUTDOWN) break;
		else if (target->response == SMP_RESP_INV_CMD) {
			return; // Faliure to shutdown, highly highly highly unlikely
		}
		__asm__ volatile("pause");
	}
	if (kget_ms_passed() - timeout > 1000) return; // Faliure to shutdown

	// Broadcast INIT IPI to reset the AP
	lapic_write(LAPIC_REG_ICR_HIGH, target->lapic_id << 24);
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004500); // INIT, level=assert
	kdelay(10);

	// Deassert INIT3
	lapic_write(LAPIC_REG_ICR_LOW, 0x00004000); // INIT deassert
	kdelay(10);

	rflags = spin_lock_irqsave(&boot_lock);
	ap_boot_flag = AOS_FALSE;

	if (target->idle_thread) {
		target->idle_thread->status = THREAD_STATUS_RUNNING;
	}
	target->status = CORE_STATUS_READY;
	target->command = 0;

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

	timeout = kget_ms_passed();
	while(!ap_boot_flag && kget_ms_passed() - timeout < 1000) { __asm__ volatile("pause");}

	if (!ap_boot_flag) {
		serial_printf("[SMP] Error: Core %lld failed to check in!\n", core_idx);
	} else {
		serial_printf("[SMP] Core %lld checked in successfully.\n", core_idx);
	}

	spin_unlock_irqrestore(&boot_lock, rflags);
}

void smp_tlb_core(uint32_t core_idx, uint64_t virt, aos_bool full_flush) {
	if (core_idx >= SMP_MAX_CORES || cores[core_idx] == NULL) return;

	if (core_idx == bsp_core_idx) {
		serial_print("[SMP] Warning: TLB Flush/Invlpage called on BSP Core! Blocked.\n");
		return;
	}

    struct core_state* target = cores[core_idx];

    uint64_t flags = spin_lock_irqsave(&target->command_lock);

	target->tlb_resp = 0;
	target->tlb_cmd = full_flush ? SMP_TLB_CMD_REFRESH_PAGES : SMP_TLB_CMD_INVLPAGE;
    target->tlb_addr = virt;

	send_wakeup_ipi(target->lapic_id, SMP_TLB_IPI_VECTOR);

	uint64_t timeout = kget_ms_passed();
	while (target->tlb_resp == 0 && kget_ms_passed() - timeout < 1000) {
		if (target->tlb_resp & (full_flush ? SMP_TLB_RESP_ACK_REFRESH_PAGES : SMP_TLB_RESP_ACK_INVPAGE)) break;
		__asm__ volatile("pause");
	}
	if (kget_ms_passed() - timeout > 1000) {
		serial_printf("[SMP] Error: Core %lld failed to check in on TLB Flush/Invlpage!\n", core_idx);
	}

	spin_unlock_irqrestore(&target->command_lock, flags);
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
