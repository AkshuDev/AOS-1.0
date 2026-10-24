#pragma once

#include <pefi.h>
#include <pefi_simple_text_in.h>
#include <pefilib.h>

#include <uniboot.h>

#include <inc/drivers/io/io.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

#define UEFI_BOX_DOUBLE_TOP_RIGHT u"\u2554"
#define UEFI_BOX_DOUBLE_TOP_LEFT u"\u2557"
#define UEFI_BOX_DOUBLE_BOTTOM_RIGHT u"\u255A"
#define UEFI_BOX_DOUBLE_BOTTOM_LEFT u"\u255D"
#define UEFI_BOX_DOUBLE_HORIZONTAL u"\u2550"
#define UEFI_BOX_DOUBLE_VERTICAL u"\u2551"

EFIAPI void uefi_printf(const char* fmt, ...);
EFIAPI void vmem_flush(void);
EFIAPI void vmem_printwc(struct VMemDesign* design, CHAR16* c);
EFIAPI void kdelay(uint32_t ms);
EFIAPI uint64_t kget_ms_passed(void);
EFIAPI uint64_t kget_timestamp_seconds(void);
EFIAPI uint64_t kget_timestamp_ms(void);
EFIAPI void ktimer_calibrate(void);
EFIAPI void uniboot_debug_dump(uniboot_hdr* header, struct VMemDesign* cursor);

#define vmem_set_cursor(d, px, py) { pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, (px), (py)); (d)->x=(px); (d)->y=(py); }
#define vmem_set_only_cursor(x, y) pefi_state.system_table->ConOut->SetCursorPosition(pefi_state.system_table->ConOut, (x), (y));
#define vmem_enable_cursor() pefi_state.system_table->ConOut->EnableCursor(pefi_state.system_table->ConOut, TRUE)
#define vmem_disable_cursor() pefi_state.system_table->ConOut->EnableCursor(pefi_state.system_table->ConOut, FALSE)
#define vmem_nbuf_printc(design, c) { \
	vmem_printc((design), (c)); \
	vmem_flush(); \
}