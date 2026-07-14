#pragma

#include <aos_inttypes.h>
#include <pefilib.h>

EFIAPI aos_bool try_load_elf(uint8_t* data, uint64_t size, uint64_t* entry, uint64_t* stack_base_out, uint64_t* stack_top_out) __attribute__((used));