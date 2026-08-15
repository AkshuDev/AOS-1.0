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

enum seek_mode {
	FS_SEEK_MODE_CUR,
	FS_SEEK_MODE_SET,
	FS_SEEK_MODE_END
};

struct aos_dmm_handler {
	uint32_t sign;
	aos_bool valid;

	char path[PBFS_MAX_PATH_LEN];
	PBFS_DMM_Entry entry;

	// Buffers
	uint8_t* file_data;
	uint64_t file_data_size;

	aos_bool file_data_refresh_req;
	aos_bool file_data_flush_required;

	spinlock_t file_lock;
};

struct aos_file {
	struct aos_dmm_handler* handle; // All info
	uint64_t cur_seek;

	uint64_t (*tell)(struct aos_file* self); // Returns current seek position
	aos_bool (*seek)(struct aos_file* self, uint64_t seek_additive, enum seek_mode seek_mode); // Seeks to a position in the file (SEEK_END causes seek_to to be used for backwards seeking)
	aos_bool (*read)(struct aos_file* self, uint64_t size, void* buf); // Reads the file contents into 'buf'
	aos_bool (*write)(struct aos_file* self, uint64_t size, void* buf); // Writes the contents of 'buf' to the file
	aos_bool (*flush)(struct aos_file* self); // Flushes all writes to PBFS
};

void aos_fs_init(struct pbfs_mount* mnt) __attribute__((used));
struct aos_file* fs_open(const char* path) __attribute__((used));
void fs_close(struct aos_file* f) __attribute__((used));