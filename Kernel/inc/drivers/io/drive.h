#pragma once

#include <aos_inttypes.h>
#include <system.h>

#include <inc/core/pcie.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

struct AOS_Module;

typedef struct drive_device {
    const char* name;
    pcie_device_t* pcie_device;

    // Function pointers
    aos_bool (*init)(struct AOS_Module* m);
    aos_bool (*read_blk)(int port_id, uint64_t lba, uint32_t count, void* buffer);
    aos_bool (*write_blk)(int port_id, uint64_t lba, uint32_t count, void* buffer);
    aos_bool (*flush)(int port_id);
    aos_bool (*get_block_device)(int port_id, struct block_device* out);

    struct block_device block_dev;
    int cur_port;

    aos_bool active;
} drive_device_t;

aos_bool get_available_drives(struct drive_device* out) __attribute__((used));
aos_bool get_available_drives_pcie(struct drive_device* out, struct aos_sysinfo_pcie pcie) __attribute__((used));