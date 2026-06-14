#pragma once
#include <inc/drivers/io/drive.h>

void start_panic_shell(struct drive_device* current_drive, const char* err, size_t err_lines) __attribute__((used));