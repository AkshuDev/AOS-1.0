#include <aos_inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/mm/avmf.h>

#include <inc/core/fs.h>

#ifdef PBFS_WDRIVERS
	#undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs_structs.h>
#include <PBFS/headers/pbfs-fs.h>

#define HANDLER_LIST_ALLOC_STEP 256

static struct pbfs_mount* gmnt;

static aos_bool read_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!gmnt || !self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	uint64_t f_rflags = spin_lock_irqsave(&self->file_lock);
	if (self->closed) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	struct aos_dmm_handler* h = self->handle;
	if (!h) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }
	if (h->sign != AOS_FS_SIGN) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	uint64_t rflags = spin_lock_irqsave(&h->handle_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			serial_printf("[FS:Read] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}

	if (self->cur_seek > h->file_data_size || size > h->file_data_size - self->cur_seek) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_FALSE;
	}
	memcpy(buf, h->file_data + self->cur_seek, size);

	self->cur_seek += size;

	spin_unlock_irqrestore(&h->handle_lock, rflags);
	spin_unlock_irqrestore(&self->file_lock, f_rflags);

	return AOS_TRUE;
}

static aos_bool write_file(struct aos_file* self, uint64_t size, void* buf) {
	if (!gmnt || !self || !buf) return AOS_FALSE;
	if (size == 0) return AOS_TRUE; // Dont need to do anything

	uint64_t f_rflags = spin_lock_irqsave(&self->file_lock);
	if (self->closed) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	struct aos_dmm_handler* h = self->handle;
	if (!h) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }
	if (h->sign != AOS_FS_SIGN) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	uint64_t rflags = spin_lock_irqsave(&h->handle_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			serial_printf("[FS:Write] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}

	// NOTE: Doesn't Increase File size.
	// TODO: Add auto file buf size increase
	if (self->cur_seek > h->file_data_size || size > h->file_data_size - self->cur_seek) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_FALSE;
	}
	
	memcpy(h->file_data + self->cur_seek, buf, size);
	h->file_data_flush_required = AOS_TRUE;

	self->cur_seek += size;

	spin_unlock_irqrestore(&h->handle_lock, rflags);
	spin_unlock_irqrestore(&self->file_lock, f_rflags);

	return AOS_TRUE;
}

static aos_bool flush_file(struct aos_file* self) {
	if (!gmnt || !self) return AOS_FALSE;

	uint64_t f_rflags = spin_lock_irqsave(&self->file_lock);

	struct aos_dmm_handler* h = self->handle;
	if (!h) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }
	if (h->sign != AOS_FS_SIGN) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	uint64_t rflags = spin_lock_irqsave(&h->handle_lock);

	if (!h->file_data_flush_required) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_TRUE;
	}
	if (!h->file_data && h->file_data_size > 0) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_FALSE;
	}

	if (h->file_data_size == 0) {
		const char* d = "\0";
		int out = pbfs_update_file_dmm(gmnt, h->path, &h->entry, (uint8_t*)d, 1);
		if (out != PBFS_RES_SUCCESS) {
			serial_printf("[FS:Flush] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}
	} else {
		int out = pbfs_update_file_dmm(gmnt, h->path, &h->entry, h->file_data, h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			serial_printf("[FS:Flush] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}
	}

	if (h->file_data) avmf_free((uint64_t)h->file_data);
	h->file_data = NULL;
	h->file_data_size = 0;

	h->file_data_flush_required = AOS_FALSE;
	h->file_data_refresh_req = AOS_TRUE;

	spin_unlock_irqrestore(&h->handle_lock, rflags);
	spin_unlock_irqrestore(&self->file_lock, f_rflags);

	return AOS_TRUE;
}

static uint64_t tell_file(struct aos_file* self) {
	if (!gmnt || !self) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&self->file_lock);
	if (self->closed) { spin_unlock_irqrestore(&self->file_lock, rflags); return 0; }

	uint64_t out = self->cur_seek;
	spin_unlock_irqrestore(&self->file_lock, rflags);

	return out;
}

static aos_bool seek_file(struct aos_file* self, uint64_t seek_to, enum seek_mode seek_mode) {
	if (!gmnt || !self) return AOS_FALSE;

	uint64_t f_rflags = spin_lock_irqsave(&self->file_lock);
	if (self->closed) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	struct aos_dmm_handler* h = self->handle;
	if (!h) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }
	if (h->sign != AOS_FS_SIGN) { spin_unlock_irqrestore(&self->file_lock, f_rflags); return AOS_FALSE; }

	uint64_t rflags = spin_lock_irqsave(&h->handle_lock);
	if (!h->file_data || h->file_data_refresh_req) {
		if (h->file_data) avmf_free((uint64_t)h->file_data);
		h->file_data = NULL;
		h->file_data_size = 0;

		int out = pbfs_read_file_dmm(gmnt, h->path, &h->entry, &h->file_data, &h->file_data_size);
		if (out != PBFS_RES_SUCCESS) {
			serial_printf("[FS:Seek] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}

		h->file_data_refresh_req = AOS_FALSE;
	}

	if (seek_to > h->file_data_size) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_FALSE;
	}

	uint64_t sto = seek_to;
	switch(seek_mode) {
		case FS_SEEK_MODE_CUR: {
			if (seek_to > h->file_data_size - self->cur_seek) {
				spin_unlock_irqrestore(&h->handle_lock, rflags);
				spin_unlock_irqrestore(&self->file_lock, f_rflags);
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
			spin_unlock_irqrestore(&h->handle_lock, rflags);
			spin_unlock_irqrestore(&self->file_lock, f_rflags);
			return AOS_FALSE;
		}
	}
	if (sto > h->file_data_size) {
		spin_unlock_irqrestore(&h->handle_lock, rflags);
		spin_unlock_irqrestore(&self->file_lock, f_rflags);
		return AOS_FALSE;
	}

	self->cur_seek = sto;

	spin_unlock_irqrestore(&h->handle_lock, rflags);
	spin_unlock_irqrestore(&self->file_lock, f_rflags);

	return AOS_TRUE;
}

void aos_fs_init(struct pbfs_mount* mnt) {
	if (!mnt) return;
	if (!mnt->active) return;
	gmnt = mnt;
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

	struct aos_file* file = (struct aos_file*)avmf_alloc(sizeof(struct aos_file), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!file) return NULL;
	memset(file, 0, sizeof(struct aos_file));

	file->handle = (struct aos_dmm_handler*)avmf_alloc(sizeof(struct aos_dmm_handler), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!file->handle) {
		avmf_free((uint64_t)file);
		return NULL;
	}
	memset(file->handle, 0, sizeof(struct aos_dmm_handler));

	file->closed = AOS_FALSE;
	file->cur_seek = 0;
	file->file_lock = 0;

	file->read = read_file;
	file->write = write_file;
	file->flush = flush_file;
	file->seek = seek_file;
	file->tell = tell_file;
	
	file->handle->sign = AOS_FS_SIGN;
	file->handle->handle_lock = 0;
	file->handle->entry = e;
	
	file->handle->file_data = NULL;
	file->handle->file_data_size = 0;
	file->handle->file_data_refresh_req = AOS_TRUE;
	file->handle->file_data_flush_required = AOS_FALSE;

	memcpy(file->handle->path, path, path_len);
	file->handle->path[path_len] = '\0';

	if (file_exists) {
		int out = pbfs_read_file_dmm(gmnt, path, &e, &file->handle->file_data, &file->handle->file_data_size);
		if (out == PBFS_RES_SUCCESS) {
			file->handle->file_data_refresh_req = AOS_FALSE;
		} else {
			serial_printf("[FS:Open] PBFS Error: %s\n", pbfs_get_err_str((enum PBFS_Result)out));
		}
	} else {
		file->handle->file_data = NULL;
		file->handle->file_data_size = 0;
	}
	
	file->handle->valid = AOS_TRUE;
	return file;
}

void fs_close(struct aos_file* f) {
	if (!f) return;

	uint64_t f_rflags = spin_lock_irqsave(&f->file_lock);
	if (f->closed) { spin_unlock_irqrestore(&f->file_lock, f_rflags); return; }

	f->closed = AOS_TRUE;
	spin_unlock_irqrestore(&f->file_lock, f_rflags);

	f->flush(f);
	f_rflags = spin_lock_irqsave(&f->file_lock);

	struct aos_dmm_handler* h = f->handle;

	if (!h) goto final_exit;
	if (h->sign != AOS_FS_SIGN) goto final_exit;

	uint64_t rflags = spin_lock_irqsave(&h->handle_lock);
	
	if (h->file_data) avmf_free((uint64_t)h->file_data);
	h->file_data_refresh_req = AOS_TRUE;
	h->file_data = NULL;
	h->file_data_size = 0;
	h->file_data_flush_required = AOS_FALSE;
	h->valid = AOS_FALSE;

	spin_unlock_irqrestore(&h->handle_lock, rflags);

	final_exit: {
		if (h) avmf_free((uint64_t)h);
		spin_unlock_irqrestore(&f->file_lock, f_rflags);

		avmf_free((uint64_t)f);
	}
}
