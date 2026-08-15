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

static aos_bool setup_ifndone_handler_list(uint64_t extra_entries_needed) {
	if (handler_list && handler_list_count + extra_entries_needed <= handler_list_cap) return AOS_TRUE;
	if (handler_list_cap < handler_list_count) handler_list_count = handler_list_cap;

	uint64_t step = HANDLER_LIST_ALLOC_STEP;
	while (extra_entries_needed > step) {
		step += HANDLER_LIST_ALLOC_STEP;
	}

	if (!handler_list) {
		struct aos_dmm_handler* nptr = (struct aos_dmm_handler*)avmf_alloc(sizeof(struct aos_dmm_handler)*step, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
		if (!nptr) return AOS_FALSE;
		handler_list = nptr;
		handler_list_cap = step;
		handler_list_count = 0;
	} else {
		struct aos_dmm_handler* nptr = (struct aos_dmm_handler*)avmf_alloc(sizeof(struct aos_dmm_handler)*(handler_list_cap + step), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
		if (!nptr) return AOS_FALSE;
		memcpy(nptr, handler_list, handler_list_count*sizeof(struct aos_dmm_handler));
		avmf_free((uint64_t)handler_list);
		handler_list = nptr;
		handler_list_cap += step;
	}
	return AOS_TRUE;
}

static uint64_t find_entry_in_handler_list(PBFS_DMM_Entry e, aos_bool* valid) {
	*valid = AOS_FALSE;
	if (!handler_list) return 0;
	for (uint64_t i = 0; i < handler_list_count; i++) {
		struct aos_dmm_handler* handler = &handler_list[i];
		if (handler->sign != AOS_FS_SIGN) continue;

		if (memcmp(&handler->entry, &e, sizeof(PBFS_DMM_Entry)) == 0) {
			*valid = AOS_TRUE;
			return i;
		}
	}
	return 0;
}

static uint64_t find_free_entry_in_handler_list(aos_bool* valid) {
	*valid = AOS_FALSE;
	if (!handler_list) return 0;
	for (uint64_t i = 0; i < handler_list_count; i++) {
		struct aos_dmm_handler* handler = &handler_list[i];
		if (handler->sign != AOS_FS_SIGN) continue;

		if (!handler->valid) {
			*valid = AOS_TRUE;
			return i;
		}
	}
	return 0;
}

static aos_bool read_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!gmnt || !self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&h->file_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}
	if (self->cur_seek > h->file_data_size || size > h->file_data_size - self->cur_seek) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_FALSE;
	}

	memcpy(buf, h->file_data + self->cur_seek, size);
	spin_unlock_irqrestore(&h->file_lock, rflags);

	return AOS_TRUE;
}

static aos_bool write_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!gmnt || !self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&h->file_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}
	// NOTE: Doesn't Increase File size. TODO: Add auto file buf size increase
	if (self->cur_seek > h->file_data_size || size > h->file_data_size - self->cur_seek) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_FALSE;
	}
	
	memcpy(h->file_data + self->cur_seek, buf, size);
	h->file_data_flush_required = AOS_TRUE;

	spin_unlock_irqrestore(&h->file_lock, rflags);

	return AOS_TRUE;
}

static aos_bool flush_file(struct aos_file* self) {
	if (!gmnt || !self) return AOS_FALSE;

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&h->file_lock);

	if (!h->file_data_flush_required) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_TRUE;
	}
	if (!h->file_data && h->file_data_size > 0) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_FALSE;
	}

	if (h->file_data_size == 0) {
		const char* d = "\0";
		if (pbfs_update_file_dmm(gmnt, h->path, &h->entry, (uint8_t*)d, 1) != PBFS_RES_SUCCESS) {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}
	} else {
		if (pbfs_update_file_dmm(gmnt, h->path, &h->entry, h->file_data, h->file_data_size) != PBFS_RES_SUCCESS) {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}
	}

	if (h->file_data) avmf_free((uint64_t)h->file_data);
	h->file_data = NULL;
	h->file_data_size = 0;

	h->file_data_flush_required = AOS_FALSE;
	h->file_data_refresh_req = AOS_TRUE;

	spin_unlock_irqrestore(&h->file_lock, rflags);

	return AOS_TRUE;
}

static uint64_t tell_file(struct aos_file* self) {
	if (!gmnt || !self) return AOS_FALSE;

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&h->file_lock);
	uint64_t out = self->cur_seek;
	spin_unlock_irqrestore(&h->file_lock, rflags);

	return out;
}

static aos_bool seek_file(struct aos_file* self, uint64_t seek_to, enum seek_mode seek_mode) {
	if (!gmnt || !self) return AOS_FALSE;

	struct aos_dmm_handler* h = self->handle;
	if (!h) return AOS_FALSE;
	if (h->sign != AOS_FS_SIGN) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&h->file_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}

	if (seek_to > h->file_data_size) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_FALSE;
	}

	uint64_t sto = seek_to;
	switch(seek_mode) {
		case FS_SEEK_MODE_CUR: {
			if (seek_to > h->file_data_size - self->cur_seek) {
				spin_unlock_irqrestore(&h->file_lock, rflags);
				return AOS_FALSE;
			}
			sto += self->cur_seek;
			break;
		}
		case FS_SEEK_MODE_END: {
			sto = h->file_data_size - seek_to;
			break;
		}
		case FS_SEEK_MODE_SET: break;
		default: {
			spin_unlock_irqrestore(&h->file_lock, rflags);
			return AOS_FALSE;
		}
	}
	if (sto > h->file_data_size) {
		spin_unlock_irqrestore(&h->file_lock, rflags);
		return AOS_FALSE;
	}

	self->cur_seek = sto;

	spin_unlock_irqrestore(&h->file_lock, rflags);

	return AOS_TRUE;
}

void aos_fs_init(struct pbfs_mount* mnt) {
	if (!mnt) return;
	if (!mnt->active) return;
	fs_lock = 0;
	gmnt = mnt;
	handler_list = NULL;
	handler_list_cap = 0;
}

struct aos_file* fs_open(const char* path) {
	if (!gmnt || !path) return NULL;

	uint64_t path_len = strlen(path);
	if (path_len >= PBFS_MAX_PATH_LEN) return NULL; // NULL Terminator requires +1 byte

	PBFS_DMM_Entry e = {0};
	uint64_t elba = 0;
	aos_bool file_exists = AOS_TRUE;
	int out_e = pbfs_find_entry(path, &e, &elba, gmnt);
	if (out_e != PBFS_RES_SUCCESS) {
		//if (out_e != PBFS_ERR_File_Not_Found) return NULL;
		//file_exists = AOS_FALSE;
		return NULL; // TODO: Add Non-Existing file support
	}

	uint64_t rflags = spin_lock_irqsave(&fs_lock);
	if (!setup_ifndone_handler_list(1)) {
		spin_unlock_irqrestore(&fs_lock, rflags);
		return NULL;
	}
	spin_unlock_irqrestore(&fs_lock, rflags);

	struct aos_file* file = (struct aos_file*)avmf_alloc(sizeof(struct aos_file), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!file) return NULL;

	file->read = read_file;
	file->write = write_file;
	file->flush = flush_file;
	file->seek = seek_file;
	file->tell = tell_file;

	rflags = spin_lock_irqsave(&fs_lock);

	aos_bool valid_e = AOS_FALSE;
	uint64_t idx = find_free_entry_in_handler_list(&valid_e);

	if (valid_e && idx < handler_list_count)
		file->handle = &handler_list[idx];
	else
		file->handle = &handler_list[handler_list_count++];
	memset(file->handle, 0, sizeof(struct aos_dmm_handler));
	
	file->handle->sign = AOS_FS_SIGN;

	spin_unlock_irqrestore(&fs_lock, rflags);

	uint64_t f_rflags = spin_lock_irqsave(&file->handle->file_lock);

	file->handle->entry = e;
	
	file->handle->file_data = NULL;
	file->handle->file_data_size = 0;
	file->handle->file_data_refresh_req = AOS_TRUE;

	memcpy(file->handle->path, path, path_len);
	file->handle->path[path_len] = '\0';

	if (file_exists) {
		int out = pbfs_read_file_dmm(gmnt, path, &e, &file->handle->file_data, &file->handle->file_data_size);
		if (out == PBFS_RES_SUCCESS) file->handle->file_data_refresh_req = AOS_FALSE;
	} else {
		file->handle->file_data = NULL;
		file->handle->file_data_size = 0;
	}
	file->handle->valid = AOS_TRUE;

	spin_unlock_irqrestore(&file->handle->file_lock, f_rflags);

	return file;
}

void fs_close(struct aos_file* f) {
	
}
