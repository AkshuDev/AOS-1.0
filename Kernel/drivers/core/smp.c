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

typedef volatile int spinlock_t;
static uintptr_t lapic_base_virt = 0;

static void spin_lock(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock);
    }
}

static void spin_unlock(spinlock_t* lock) {
    __sync_lock_release(lock);
}

static void lapic_init(uintptr_t phys_addr) {
    lapic_base_virt = phys_addr + AOS_DIRECT_MAP_BASE;
}

static void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic_base_virt + reg) = value;
    (void)*(volatile uint32_t*)(lapic_base_virt + reg);
}

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(lapic_base_virt + reg);
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

static void send_ipi(uint8_t target_apic_id, uint8_t vector) {
    uintptr_t lapic = (uintptr_t)acpi_get_lapic_base();

    // init IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C500); // ICR Low: INIT
    mdelay(10);

    while (lapic_read(0x300) & (1 << 12)) { asm("pause"); }

    // Startup IPI
    lapic_write(0x310, (target_apic_id << 24)); // ICR High
    lapic_write(0x300, 0x0000C600 | vector); // ICR Low: STARTUP
}

void ap_kernel_entry(void) {
    idt_load_local();
    ap_boot_flag = 1;
    serial_printf("[SMP] Core %d online!\n", get_lapic_id());

    for (;;) {
        asm("hlt");
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

    for (uint64_t i = 0; i < core_count; i++) {
        uint8_t id = apic_ids[i];
        if (id == bsp_apic_id) continue;

        spin_lock(&boot_lock);
        ap_boot_flag = 0;

        uint64_t ap_stack_phys = 0;
        void* ap_stack = (void*)avmf_alloc(16384, MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &ap_stack_phys);
        if (!ap_stack) {
            serial_printf("[SMP] Error: Could not allocate stack for core %lld\n", id);
            spin_unlock(&boot_lock);
            continue;
        }

        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x510) = (uintptr_t)ap_stack + 16384;
        *(uint64_t*)(AOS_DIRECT_MAP_BASE + 0x518) = (uintptr_t)ap_kernel_entry;

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
}
