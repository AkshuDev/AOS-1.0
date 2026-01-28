#include <inc/kexceptions.h>

#include <inttypes.h>
#include <asm.h>

#include <inc/io.h>

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

void aos_system_exception(regs_t *r) {
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
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
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

    serial_print("\nSYSTEM HALTED\n");
    for (;;) {
        asm volatile ("cli; hlt");
    }
}
