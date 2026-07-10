#include <inc/core/kexceptions.h>
#include <inc/core/kfuncs.h>
#include <inc/core/smp.h>
#include <inc/core/acpi.h>

#include <aos_inttypes.h>
#include <asm.h>

#include <inc/drivers/io/io.h>

static const char *exception_names[] = {
    "Divide-by-Zero (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt (#NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 Floating-Point Exception (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

static volatile uint8_t panic_depth[SMP_MAX_CORES];
static volatile spinlock_t panic_lock;
static void (*pre_halt_system)(void);

void aos_system_exception_handler_init(void (*ppre_halt_system)(void)) {
	pre_halt_system = ppre_halt_system;
}

void aos_system_exception(struct reg_trap_frame *r) {
	asm volatile("cli");

	uint32_t core_idx = smp_get_current_core();

	uint64_t rflags = spin_lock_irqsave(&panic_lock);
	panic_depth[core_idx]++;
	uint64_t lpanic = panic_depth[core_idx];
	spin_unlock_irqrestore(&panic_lock, rflags);
	if (lpanic == 2) {
		serial_print("\nNESTED PANIC - STAGE 1 (Full reboot)\n");
		
		pre_halt_system();

		acpi_reboot();
        for(;;) asm volatile("hlt");
	} else if (lpanic == 3) {
		serial_print("\nNESTED PANIC - STAGE 2 (Emergency reboot)\n");

		acpi_reboot();
        for(;;) asm volatile("hlt");
	} else if (lpanic == 4) {
		serial_print("\nNESTED PANIC - STAGE 3 (Direct halt)\n");
        for(;;) asm volatile("hlt");
	} else if (lpanic > 4) {
        for(;;) asm volatile("hlt");
	}

	asm volatile("cli");

	uint64_t cr2 = 0;
	if (r->int_no == 14)
		asm volatile("mov %%cr2,%0":"=r"(cr2));

    uint64_t num = r->int_no;
    const char* name = (num < 32) ? exception_names[num] : "Unknown Exception";

    serial_print("\n==================================================\n"); // I wasted time on this, jk i used a website
    serial_print("           AOS: CPU EXCEPTION OCCURRED!           \n");
    serial_print("==================================================\n");
    serial_printf("Exception: %s (#%llu)\n", name, num);
    serial_printf("Error Code: 0x%llx\n\n", r->err_code);
	
    serial_printf("RIP: %016llx  CS:  %04llx  RFLAGS: %016llx\n", r->rip, r->cs, r->rflags);
    serial_printf("RSP: %016llx  SS:  %04llx  RBP:    %016llx\n", r->rsp, r->ss, r->rbp);
    serial_printf("RAX: %016llx  RBX: %016llx  RCX:    %016llx\n", r->rax, r->rbx, r->rcx);
    serial_printf("RDX: %016llx  RSI: %016llx  RDI:    %016llx\n", r->rdx, r->rsi, r->rdi);
    serial_printf("R8:  %016llx  R9:  %016llx  R10:    %016llx\n", r->r8, r->r9, r->r10);
    serial_printf("R11: %016llx  R12: %016llx  R13:    %016llx\n", r->r11, r->r12, r->r13);
    serial_printf("R14: %016llx  R15: %016llx\n", r->r14, r->r15);
    serial_print("\n--- Decoding Details ---\n");

    // decode specific exceptions
    // Page Fault (#PF)
    if (num == 14) {
        serial_printf("Faulting Address (CR2): 0x%016llx\n", cr2);
        serial_printf(
            "Reason: %s, %s, %s%s%s\n",
            (r->err_code & 0x01) ? "Protection violation" : "Non-present page",
            (r->err_code & 0x02) ? "Write" : "Read",
            (r->err_code & 0x04) ? "User-mode" : "Kernel-mode",
            (r->err_code & 0x08) ? ", Reserved bit set" : "",
            (r->err_code & 0x10) ? ", Instruction fetch" : ""
        );
    }

    // General Protection Fault Decoding (#GP)
    else if (num == 13 || num == 10 || num == 11 || num == 12) {
        if (r->err_code == 0) {
            serial_print("Reason: General violation (No selector involved).\n");
        } else {
            serial_printf(
                "Reason: Selector Error Index: 0x%llx, Table: %s, %s\n",
                (r->err_code >> 3) & 0x1FFF,
                (r->err_code & 0x04) ? "LDT" : ((r->err_code & 0x02) ? "IDT" : "GDT"),
                (r->err_code & 0x01) ? "External to CPU" : "Internal"
            );
        }
    }

    // Invalid Opcode Decoding (#UD)
    else if (num == 6) {
        serial_print("Reason: The CPU tried to execute an undefined instruction.\n");
        serial_print("Common causes: Jumping to data/stack, or SSE/AVX used without being enabled.\n");
    }

    // Double Fault Decoding (#DF)
    else if (num == 8) {
        serial_print("CRITICAL: Double Fault. The CPU failed to invoke an earlier exception handler.\n");
        serial_print("Usually caused by a kernel stack overflow or a fault inside the Page Fault handler.\n");
    }

	if (!smp_is_bsp_core()) {
		uint32_t core = smp_get_current_core();
		serial_printf("\nRESETTING CORE %u\n", core);
		smp_reset_core(core);
		smp_yield();
		return;
	}

	serial_print("\nREBOOTING SYSTEM\n");
	
	// Print on screen - (done later to allow all serial information to pass incase of a nested panic in )

	struct VMemDesign c = {
		.bg = VMEM_COLOR_BLACK,
		.fg = VMEM_COLOR_WHITE,
		.serial_out = 0,
		.x = 0,
		.y = 0
	};
	vmem_clear_screen(&c);
	vmem_print(&c, "AOS: CPU EXCEPTION OCCURRED!\n");
    vmem_printf(&c, "Exception: %s (#%llu)\n", name, num);
    vmem_printf(&c, "Error Code: 0x%llx\n\n", r->err_code);
	
    vmem_printf(&c, "RIP: %016llx  CS:  %04llx  RFLAGS: %016llx\n", r->rip, r->cs, r->rflags);
    vmem_printf(&c, "RSP: %016llx  SS:  %04llx  RBP:    %016llx\n", r->rsp, r->ss, r->rbp);
    vmem_printf(&c, "RAX: %016llx  RBX: %016llx  RCX:    %016llx\n", r->rax, r->rbx, r->rcx);
    vmem_printf(&c, "RDX: %016llx  RSI: %016llx  RDI:    %016llx\n", r->rdx, r->rsi, r->rdi);
    vmem_printf(&c, "R8:  %016llx  R9:  %016llx  R10:    %016llx\n", r->r8, r->r9, r->r10);
    vmem_printf(&c, "R11: %016llx  R12: %016llx  R13:    %016llx\n", r->r11, r->r12, r->r13);
    vmem_printf(&c, "R14: %016llx  R15: %016llx\n", r->r14, r->r15);
    vmem_print(&c, "\n--- Decoding Details ---\n");

	// decode specific exceptions
    // Page Fault (#PF)
    if (num == 14) {
        vmem_printf(&c, "Faulting Address (CR2): 0x%016llx\n", cr2);
        vmem_printf(
			&c,
            "Reason: %s, %s, %s%s%s\n",
            (r->err_code & 0x01) ? "Protection violation" : "Non-present page",
            (r->err_code & 0x02) ? "Write" : "Read",
            (r->err_code & 0x04) ? "User-mode" : "Kernel-mode",
            (r->err_code & 0x08) ? ", Reserved bit set" : "",
            (r->err_code & 0x10) ? ", Instruction fetch" : ""
        );
    }

    // General Protection Fault Decoding (#GP)
    else if (num == 13 || num == 10 || num == 11 || num == 12) {
        if (r->err_code == 0) {
            vmem_print(&c, "Reason: General violation (No selector involved).\n");
        } else {
            vmem_printf(
				&c,
                "Reason: Selector Error Index: 0x%llx, Table: %s, %s\n",
                (r->err_code >> 3) & 0x1FFF,
                (r->err_code & 0x04) ? "LDT" : ((r->err_code & 0x02) ? "IDT" : "GDT"),
                (r->err_code & 0x01) ? "External to CPU" : "Internal"
            );
        }
    }

    // Invalid Opcode Decoding (#UD)
    else if (num == 6) {
        vmem_print(&c, "Reason: The CPU tried to execute an undefined instruction.\n");
        vmem_print(&c, "Common causes: Jumping to data/stack, or SSE/AVX used without being enabled.\n");
    }

    // Double Fault Decoding (#DF)
    else if (num == 8) {
        vmem_print(&c, "CRITICAL: Double Fault. The CPU failed to invoke an earlier exception handler.\n");
        vmem_print(&c, "Usually caused by a kernel stack overflow or a fault inside the Page Fault handler.\n");
    }

	vmem_print(&c, "\nREBOOTING SYSTEM\n");

	if (pre_halt_system) pre_halt_system();
	acpi_reboot();
	for (;;) __asm__ volatile("hlt");
}
