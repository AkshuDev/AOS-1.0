#pragma once

#include <pefi.h>

EFIAPI void uefi_printf(const char* fmt, ...) __attribute__((used));