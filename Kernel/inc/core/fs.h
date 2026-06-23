#pragma once

#include <aos_inttypes.h>

#include <inc/core/kfuncs.h>

#ifdef PBFS_WDRIVERS
	#undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs_structs.h>
#include <PBFS/headers/pbfs-fs.h>

#define AOS_FS_SIGN 0xA1519F19 // AOS FS

struct aos_dmm_handler {
	uint32_t sign;
	PBFS_DMM_Entry entry;

	spinlock_t file_lock;
};

struct aos_file {
	struct aos_dmm_handler* handle; // All info

	aos_bool (*read)(struct aos_file* self, uint64_t size, void* buf);
	aos_bool (*write)(struct aos_file* self, uint64_t size, void* buf);
	aos_bool (*flush)(struct aos_file* self);
};

void aos_fs_init(struct pbfs_mount* mnt) __attribute__((used));
uint64_t fs_open(const char* path) __attribute__((used));