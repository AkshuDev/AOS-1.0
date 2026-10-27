#pragma once

#include <PBFS/headers/pbfs-fs.h>

#define KERNEL_UID 0
#define KERNEL_GID 0

struct pbfs_mount* aos_get_mounted_fs(void);
void aos_vmss_start(void);
void aos_pre_halt_system(void);
void aos_create_path(char* out, const char* cwd, const char* in);