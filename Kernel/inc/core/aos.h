#pragma once

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

struct pbfs_mount* aos_get_mounted_fs(void);
void aos_vmss_start(void);
void aos_pre_halt_system(void);
void aos_create_path(char* out, const char* cwd, const char* in);