#pragma once

#include <inttypes.h>
#include <inc/core/pcie.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

typedef struct drive_device {
    const char* name;
    pcie_device_t* pcie_device;

    // Function pointers
    int (*init)(void);
    int (*read_blk)(int port_id, uint64_t lba, uint32_t count, void* buffer);
    int (*write_blk)(int port_id, uint64_t lba, uint32_t count, void* buffer);
    int (*flush)(int port_id);
    int (*get_block_device)(int port_id, struct block_device* out);

    struct block_device block_dev;
    int cur_port;

    uint8_t active;
} drive_device_t;

int get_available_drives(struct drive_device* out) __attribute__((used));