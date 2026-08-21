#pragma once
#include <inttypes.h>
#include <inc/drivers/gpu/apis/pyrion.h>

aos_bool aosbf_to_pyrion_font(uint8_t* data, uint64_t size, struct pyrion_font* out) __attribute__((used));
aos_bool aosbf_file_to_pyrion_font(const char* path, struct pyrion_font* out) __attribute__((used));