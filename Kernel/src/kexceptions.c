#include <inc/kexceptions.h>

#include <inttypes.h>
#include <asm.h>

#include <inc/io.h>

typedef struct regs_t {
    // Pushed by pushad
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp; // value before pushad
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    // Pushed automatically by the CPU
    uint32_t int_no; // optional, you can push it manually before call
    uint32_t err_code; // some exceptions push this automatically

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
} __attribute__((packed)) regs_t;

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

    serial_print("AOS: CPU EXCEPTION OCCURED!\n");
    serial_printf("Exception: %s\n", name);
    serial_printf("Interrupt #: %llu    Error Code: 0x%llx\n", num, r->err_code);
    serial_printf("EIP: %016llx  CS: %04llx  EFLAGS: %016llx\n", r->eip, r->cs, r->eflags);
    serial_printf("ESP: %016llx  SS: %04llx\n", r->esp, r->ss);
    serial_printf("EAX=%016llx  EBX=%016llx  ECX=%016llx  EDX=%016llx\n", r->eax, r->ebx, r->ecx, r->edx);
    serial_printf("ESI=%016llx  EDI=%016llx  EBP=%016llx\n", r->esi, r->edi, r->ebp);

    // decode specific exceptions
    if (num == 14) {  // Page Fault
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_printf("Page Fault Address: 0x%016llx\n", cr2);

        serial_print("Reason: ");
        if (!(r->err_code & 1)) serial_print("Page Not Present ");
        if (r->err_code & 2) serial_print("Write Access ");
        else serial_print("Read Access ");
        if (r->err_code & 4) serial_print("User Mode ");
        else serial_print("Kernel Mode ");
        if (r->err_code & 8) serial_print("Reserved Bit Violation ");
        if (r->err_code & 16) serial_print("Instruction Fetch ");
        serial_print("\n");
    }

    serial_print("\nSystem halted.\n");
    asm volatile ("cli");
    asm volatile ("hlt");
}