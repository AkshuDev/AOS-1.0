#include <aos_inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/fs.h>

#ifdef PBFS_WDRIVERS
	#undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs_structs.h>
#include <PBFS/headers/pbfs-fs.h>

#define HANDLER_LIST_ALLOC_STEP 256

static spinlock_t fs_lock;
static struct pbfs_mount* gmnt;

static struct aos_dmm_handler* handler_list;
static uint64_t handler_list_cap;
static uint64_t handler_list_count;

static uint8_t setup_ifndone_handler_list(uint64_t extra_entries_needed) {
	if (handler_list && handler_list_count + extra_entries_needed < handler_list_cap) return 1;
	if (handler_list_cap < handler_list_count) handler_list_count = handler_list_cap;

	uint64_t step = HANDLER_LIST_ALLOC_STEP;
	while (extra_entries_needed > step) {
		step += HANDLER_LIST_ALLOC_STEP;
	}

	if (handler_list) {
		struct aos_dmm_handler* nptr = (struct aos_dmm_handler*)avmf_alloc(sizeof(struct aos_dmm_handler)*step, MALLOC_TYPE_KERNEL, PAGE_PRESENT|PAGE_RW, NULL);
		if (!nptr) return 0;
		avmf_free((uint64_t)handler_list);
		handler_list = nptr;
		handler_list_cap = step;
		handler_list_count = 0;
	} else {
		struct aos_dmm_handler* nptr = (struct aos_dmm_handler*)avmf_alloc(sizeof(struct aos_dmm_handler)*(handler_list_cap + step), MALLOC_TYPE_KERNEL, PAGE_PRESENT|PAGE_RW, NULL);
		if (!nptr) return 0;
		memcpy(nptr, handler_list, handler_list_count*sizeof(struct aos_dmm_handler));
		avmf_free((uint64_t)handler_list);
		handler_list = nptr;
		handler_list_cap += step;
	}
	return 1;
}

static uint64_t find_entry_in_handler_list(PBFS_DMM_Entry e, uint8_t* valid) {
	*valid = 0;
	if (!handler_list) return 0;
	for (uint64_t i = 0; i < handler_list_count; i++) {
		struct aos_dmm_handler* handler = &handler_list[i];
		if (handler->sign != AOS_FS_SIGN) continue;

		if (memcmp(&handler->entry, &e, sizeof(PBFS_DMM_Entry)) == 0) {
			*valid = 1;
			return i;
		}
	}
	return 0;
}

static aos_bool read_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	return AOS_FALSE; // Needs PBFS read file fast Update
}

static aos_bool write_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	return AOS_FALSE; // Needs PBFS write file fast Update
}

static aos_bool flush_file(struct aos_file* self) {
	if (!self) return AOS_FALSE;

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	return AOS_FALSE; // Needs AOS flush file Update
}

void aos_fs_init(struct pbfs_mount* mnt) {
	if (!mnt) return;
	if (!mnt->active) return;
	fs_lock = 0;
	gmnt = mnt;
	handler_list = NULL;
	handler_list_cap = 0;
}

uint64_t fs_open(const char* path) {
	if (!gmnt) return 0;

	PBFS_DMM_Entry e = {0};
	uint64_t elba = 0;
	if (pbfs_find_entry(path, &e, &elba, gmnt) != PBFS_RES_SUCCESS) {
		return 0;
	}

	struct aos_file* file = (struct aos_file*)avmf_alloc(sizeof(struct aos_file), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, NULL);
	if (!file) return 0;

	uint64_t rflags = spin_lock_irqsave(&fs_lock);
	
	setup_ifndone_handler_list(1);

	aos_bool valid_e = AOS_FALSE;
	uint64_t idx = find_entry_in_handler_list(e, &valid_e);
	if (valid_e && idx < handler_list_count) {
		file->handle = &handler_list[idx];
		file->read = read_file;
		file->write = write_file;
		file->flush = flush_file;
	}

	spin_unlock_irqrestore(&fs_lock, rflags);

	return (uint64_t)file;
}