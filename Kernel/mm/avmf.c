#include <aos_inttypes.h>
#include <system.h>

#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>
#include <inc/drivers/io/io.h>

#include <stddef.h>

#define AVMF_STATIC_SIZE 1024
#define AVMF_STATIC_RANGE_SIZE 2048
#define BITMAP_SIZE 131072

static spinlock_t avmf_lock = 0;
static spinlock_t avmf_lock2 = 0;

static avmf_region_header_t regions[AVMF_STATIC_SIZE];
static uint64_t region_count;

static avmf_region_header_t physical_regions[AVMF_STATIC_SIZE];
static uint64_t physical_region_count;

static avmf_range_t static_ranges[AVMF_STATIC_RANGE_SIZE];
static uint64_t static_ranges_count;

static void* ihdr_cpage = NULL;
static uint64_t ihdr_cpage_remaining = 0;

static void* irange_cpage = NULL;
static uint64_t irange_cpage_remaining = 0;

static uint64_t avmf_bitmap[BITMAP_SIZE];

static inline uint64_t align4k(uint64_t value) {
    return ALIGN_UP(value, PAGE_SIZE);
}

static void bitmap_set(uint64_t page_idx) {
    avmf_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
}

static void bitmap_clear(uint64_t page_idx) {
    avmf_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
}

static aos_bool bitmap_test(uint64_t page_idx) {
    return avmf_bitmap[page_idx / 8] & (1 << (page_idx % 8));
}

static uint64_t avmf_alloc_phys_page(void) {
	uint64_t rflags = spin_lock_irqsave(&avmf_lock);

    for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

        uint64_t start_page = r->base / PAGE_SIZE;
        uint64_t end_page = r->limit / PAGE_SIZE;

        for (uint64_t p = start_page; p < end_page; p++) {
            if (!bitmap_test(p)) {
                bitmap_set(p);
				spin_unlock_irqrestore(&avmf_lock, rflags);

				uint64_t r_rflags = spin_lock_irqsave(&r->lock);
				r->allocated_bytes += PAGE_SIZE;
				spin_unlock_irqrestore(&r->lock, r_rflags);

                return p * PAGE_SIZE;
            }
        }
    }

	spin_unlock_irqrestore(&avmf_lock, rflags);

    serial_print("[AVMF] Out of physical memory!\n");
    return 0;
}

static void avmf_free_phys_page(uint64_t phys) {
	uint64_t rflags = spin_lock_irqsave(&avmf_lock);
	uint64_t page = ALIGN_DOWN(phys, PAGE_SIZE) / PAGE_SIZE;

    for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

        uint64_t start_page = r->base / PAGE_SIZE;
        uint64_t end_page = r->limit / PAGE_SIZE;

        if (page < start_page || page >= end_page) continue;
		
		if (!bitmap_test(page)) {
			spin_unlock_irqrestore(&avmf_lock, rflags);
            return;
        }
        bitmap_clear(page);

		uint64_t r_rflags = spin_lock_irqsave(&r->lock);
		r->allocated_bytes = r->allocated_bytes >= PAGE_SIZE ? r->allocated_bytes - PAGE_SIZE : 0;
		spin_unlock_irqrestore(&r->lock, r_rflags);

		spin_unlock_irqrestore(&avmf_lock, rflags);
        return;
    }

	spin_unlock_irqrestore(&avmf_lock, rflags);
    return;
}

static uint32_t avmf_convert_flags_to_pager_flags(uint32_t flags) {
	uint32_t out = PAGE_XD;
	if (flags & AVMF_FLAG_WRITEABLE || flags & AVMF_FLAG_READABLE) out |= PAGE_RW;
	if (flags & AVMF_FLAG_EXECUTABLE) out &= ~PAGE_XD;
	
	if (flags & AVMF_FLAG_NO_CACHE) out |= PAGE_PCD;

	if (flags & AVMF_FLAG_USERMODE) out |= PAGE_USER;

	if (flags & AVMF_FLAG_GLOBAL) out |= PAGE_GLOBAL;
	if (flags & AVMF_FLAG_DIRTY) out |= PAGE_DIRTY;
	return out;
}

static avmf_region_header_t* avmf_find_region(uint64_t min_virt, uint64_t max_virt, uint64_t min_size) {
	avmf_region_header_t* best = NULL;
	uint64_t best_score = 0;
	
	for (uint64_t i = 0; i < region_count; i++) {
		avmf_region_header_t* r = &regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

		if (r->base > min_virt) continue;

		uint64_t score = 0;
		if (r->limit > max_virt) {
			score += 1000;
		} else if (r->limit == max_virt) {
			score += 100;
		} else if (r->limit > min_size + min_virt) {
			score += 10;
		}

		if (score > best_score) best = r;
	}

	return best;
}

static avmf_region_header_t* avmf_find_region_phys(uint64_t min_phys, uint64_t max_phys, uint64_t min_size) {
	avmf_region_header_t* best = NULL;
	uint64_t best_score = 0;
	
	for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

		if (r->base > min_phys) continue;

		uint64_t score = 0;
		if (r->limit > max_phys) {
			score += 1000;
		} else if (r->limit == max_phys) {
			score += 100;
		} else if (r->limit > min_size + max_phys) {
			score += 10;
		}

		if (score > best_score) best = r;
	}

	return best;
}

static void avmf_unlink_header(avmf_region_header_t* r, avmf_header_t* h) {
	if (!h || !r) return;
	if (r->signature != AVMF_SIGNATURE) return;
    if (r->version != AVMF_VERSION) return;
    if (r->limit < 1 || r->limit <= r->base) return;

	uint64_t rflags = spin_lock_irqsave(&r->lock);

	avmf_header_type_t typ = h->type;

	aos_bool is_end = AOS_TRUE;
	aos_bool poisoned = AOS_FALSE;

	struct AVMF_Header* n = h->next;
	struct AVMF_Header* p = h->parent;
	if (n) {
		is_end = AOS_FALSE;

		if (n->signature != AVMF_SIGNATURE) poisoned = AOS_TRUE;
		if (n->version != AVMF_VERSION) poisoned = AOS_TRUE;

		if (!poisoned) n->parent = p;
		else {
			n->type = AVMF_HDR_TYPE_CORRUPT;
			n = NULL;
		}
	}
	if (p) {
		aos_bool p_poisoned = AOS_FALSE;

		if (p->signature != AVMF_SIGNATURE) p_poisoned = AOS_TRUE;
		if (p->version != AVMF_VERSION) p_poisoned = AOS_TRUE;

		if (!p_poisoned) p->next = (poisoned ? NULL : n);
		else if (!poisoned && n) { // If parent is invalid but child is valid
			n->parent = NULL;
			p->type = AVMF_HDR_TYPE_CORRUPT;
			p = NULL;
		}
		if (!poisoned && p_poisoned) poisoned = AOS_TRUE;
	}

	if (poisoned) {
		h->type = AVMF_HDR_TYPE_CORRUPT;
		spin_unlock_irqrestore(&r->lock, rflags);
		return;
	}

	switch (typ) {
		case AVMF_HDR_TYPE_ALLOC: {
			r->alloc_count--;
			if (is_end) r->alloc_list_end = h->parent;
			break;
		}
		case AVMF_HDR_TYPE_CACHE: {
			r->cache_count--;
			if (is_end) r->cache_list_end = h->parent;
			break;
		}
		default: break;
	}

	spin_unlock_irqrestore(&r->lock, rflags);
}

static aos_bool avmf_link_header(avmf_region_header_t* r, avmf_header_t* h) {
	if (!r || !h) return AOS_FALSE;
	if (r->signature != AVMF_SIGNATURE) return AOS_FALSE;
    if (r->version != AVMF_VERSION) return AOS_FALSE;
    if (r->limit < 1 || r->limit <= r->base) return AOS_FALSE;

	h->next = NULL;
	h->parent = NULL;

	struct AVMF_Header** list = NULL;
	uint64_t* count = 0;
	struct AVMF_Header** end = NULL;
	avmf_header_type_t list_type = AVMF_HDR_TYPE_CORRUPT;

	switch (h->type) {
		case AVMF_HDR_TYPE_ALLOC: {
			list = &r->alloc_list;
			count = &r->alloc_count;
			end = &r->alloc_list_end;
			list_type = AVMF_HDR_TYPE_ALLOC;
			break;
		}
		
		case AVMF_HDR_TYPE_CACHE: {
			list = &r->cache_list;
			count = &r->cache_count;
			end = &r->cache_list_end;
			list_type = AVMF_HDR_TYPE_CACHE;
			break;
		}

		default: return AOS_FALSE;
	}

	if (!list || !count || !end || list_type == AVMF_HDR_TYPE_CORRUPT) return AOS_FALSE;

	uint64_t rflags = spin_lock_irqsave(&r->lock);

	if (*count < 1 || !*list) {
		reset_list: {
			*list = h;
			*end = h;
			*count = 1;
		}
	} else {
		if (!*end) {
			no_proper_end: {
				*end = h;
				struct AVMF_Header* p = NULL;
				struct AVMF_Header* c = *list;
				for (uint64_t i = 0; i < *count; i++) {
					struct AVMF_Header* n = c->next;
					if (c->signature != AVMF_SIGNATURE || c->version != AVMF_VERSION) {
						avmf_unlink_header(r, c);

						try_continue_next_after_corrupt: {
							if (!n) {
								reset_list_from_this_point: {
									if (i == 0) goto reset_list;
									else {
										if (!p) goto reset_list;
										*count = i; // since current point is also corrupt
										p->next = h;
										h->parent = p;
									}
									break;
								}
							}
							
							if (n->type != list_type) { // Entire list from this point is corrupt
								if (i == 0) goto reset_list;
								else {
									goto reset_list_from_this_point;
								}
								break;
							} else {
								c = n;
								continue;
							}
						}
					}
					if (c->type == AVMF_HDR_TYPE_CORRUPT) {
						goto try_continue_next_after_corrupt;
						break;
					}

					if (!n) {
						c->next = h;
						h->parent = c;
						break;
					}
					p = c;
					c = n;
				}
			}
		} else {
			struct AVMF_Header* c = *end;
			if (c->signature != AVMF_SIGNATURE || c->version != AVMF_VERSION) {
				goto no_proper_end;
			}
			if (c->type == AVMF_HDR_TYPE_CORRUPT) {
				goto no_proper_end;
			}
			c->next = h;
			h->parent = c;
		}
	}

	spin_unlock_irqrestore(&r->lock, rflags);
	return AOS_TRUE;
}

static aos_bool avmf_free_list_append(avmf_region_header_t* r, avmf_range_t* range) {
    if (!r || !range) return AOS_FALSE;
    if (range->size == 0) return AOS_FALSE;

    if (r->signature != AVMF_SIGNATURE) return AOS_FALSE;
    if (r->version != AVMF_VERSION) return AOS_FALSE;
    if (r->limit < 1 || r->limit <= r->base) return AOS_FALSE;

    uint64_t range_end = range->base + range->size;

    if (!r->free_list || r->free_count == 0) {
        range->next = NULL;

        r->free_list = range;
        r->free_list_end = range;
        r->free_count = 1;

        return AOS_TRUE;
    }

    avmf_range_t* prev = NULL;
    avmf_range_t* cur = r->free_list;

    for (uint64_t i = 0; i < r->free_count; i++) {
        if (!cur) return AOS_FALSE;
        uint64_t cur_end = cur->base + cur->size;

        if (range->base < cur_end && cur->base < range_end) return AOS_FALSE;
        if (range->base < cur->base) break;

        prev = cur;
        cur = cur->next;
    }

    aos_bool merge_prev = AOS_FALSE;
    aos_bool merge_next = AOS_FALSE;

    if (prev) {
        uint64_t prev_end = prev->base + prev->size;
        if (prev_end == range->base) merge_prev = AOS_TRUE;
    }
    if (cur) {
        if (range_end == cur->base) merge_next = AOS_TRUE;
    }

    if (merge_prev) {
        prev->size += range->size;

        if (merge_next) {
            prev->size += cur->size;
            prev->next = cur->next;

            if (r->free_list_end == cur) r->free_list_end = prev;
            r->free_count--;
        }
        return AOS_TRUE;
    }
    if (merge_next) {
        range->size += cur->size;
        range->next = cur->next;

        if (prev) prev->next = range;
        else r->free_list = range;

        if (r->free_list_end == cur) r->free_list_end = range;
        r->free_count--;
        return AOS_TRUE;
    }

    range->next = cur;
    if (prev) prev->next = range;
    else r->free_list = range;

    if (!cur) r->free_list_end = range;
    r->free_count++;
    return AOS_TRUE;
}

static avmf_header_t* avmf_alloc_ihdr(void) {
	if (ihdr_cpage_remaining >= sizeof(avmf_header_t) && ihdr_cpage) {
		avmf_header_t* out = (avmf_header_t*)((uint8_t*)ihdr_cpage + (PAGE_SIZE - ihdr_cpage_remaining));
		ihdr_cpage_remaining -= sizeof(avmf_header_t);
		return out;
	} else {
		uint64_t page_ptr = 0;
		for (uint8_t try_count = 0; try_count < 3; try_count++) {
			page_ptr = avmf_alloc_phys_page();
			if (page_ptr) break;
		}
		if (!page_ptr) return NULL;
		if (!pager_map(AOS_DIRECT_MAP_BASE + page_ptr, page_ptr, PAGE_RW | PAGE_PCD | PAGE_PRESENT)) {
			avmf_free_phys_page(page_ptr);
			return NULL;
		}

		ihdr_cpage = (void*)(AOS_DIRECT_MAP_BASE + page_ptr);
		memset(ihdr_cpage, 0, PAGE_SIZE);

		avmf_header_t* out = (avmf_header_t*)ihdr_cpage;
		ihdr_cpage_remaining = PAGE_SIZE - sizeof(avmf_header_t);
		
		return out;
	}
}
static void avmf_free_ihdr(avmf_header_t* h) {
	uint64_t hptr = (uint64_t)h;
	if (hptr == (uint64_t)ihdr_cpage && ihdr_cpage_remaining == PAGE_SIZE - sizeof(avmf_header_t)) {
		ihdr_cpage_remaining = PAGE_SIZE;
	}
}

static avmf_range_t* avmf_alloc_irange(void) {
	if (irange_cpage_remaining >= sizeof(avmf_range_t) && irange_cpage) {
		avmf_range_t* out = (avmf_range_t*)((uint8_t*)irange_cpage + (PAGE_SIZE - irange_cpage_remaining));
		irange_cpage_remaining -= sizeof(avmf_range_t);
		return out;
	} else {
		uint64_t page_ptr = 0;
		for (uint8_t try_count = 0; try_count < 3; try_count++) {
			page_ptr = avmf_alloc_phys_page();
			if (page_ptr) break;
		}
		if (!page_ptr) return NULL;
		if (!pager_map(AOS_DIRECT_MAP_BASE + page_ptr, page_ptr, PAGE_RW | PAGE_PCD | PAGE_PRESENT)) {
			avmf_free_phys_page(page_ptr);
			return NULL;
		}

		irange_cpage = (void*)(AOS_DIRECT_MAP_BASE + page_ptr);
		memset(irange_cpage, 0, PAGE_SIZE);

		avmf_range_t* out = (avmf_range_t*)irange_cpage;
		irange_cpage_remaining = PAGE_SIZE - sizeof(avmf_range_t);
		
		return out;
	}
}
static void avmf_free_irange(avmf_range_t* h) {
	uint64_t hptr = (uint64_t)h;
	if (hptr == (uint64_t)irange_cpage && irange_cpage_remaining == PAGE_SIZE - sizeof(avmf_range_t)) {
		irange_cpage_remaining = PAGE_SIZE;
	}
}

static void avmf_region_init(avmf_region_header_t* r, uint64_t base, uint64_t limit) {
	memset(r, 0, sizeof(avmf_region_header_t));

	r->signature = AVMF_SIGNATURE;
	r->version = AVMF_VERSION;

	r->base = base;
	r->limit = limit;

	r->lock = 0;

	r->free_list = &static_ranges[static_ranges_count++];
	r->free_list->base = base;
	r->free_list->size = limit - base;
	r->free_list->next = NULL;
	
	r->free_count = 1;
	r->free_list_end = r->free_list;

	r->alloc_list = NULL;
	r->alloc_count = 0;
	r->alloc_list_end = NULL;

	r->cache_list = NULL;
	r->cache_count = 0;
	r->cache_list_end = NULL;
}

static avmf_header_t* avmf_region_alloc(avmf_region_header_t* r, uint64_t size, uint64_t align, uint64_t min_virt, uint64_t max_virt) {
	if (!r) return NULL;
	if (r->signature != AVMF_SIGNATURE) return NULL;
    if (r->version != AVMF_VERSION) return NULL;
    if (r->limit < 1 || r->limit <= r->base) return NULL;
	
	if (r->cache_count > 0) {
		struct AVMF_Header* c = r->cache_list;
		for (uint64_t i = 0; i < r->cache_count; i++) {
			struct AVMF_Header* n = c->next;
			if (c->type == AVMF_HDR_TYPE_CORRUPT) {
				try_continue_next_after_corrupt: {
					if (!n) {
						break;
					}
					
					if (n->type != AVMF_HDR_TYPE_CACHE) {
						break;
					} else {
						c = n;
						continue;
					}
				}
			}
			else if (c->type != AVMF_HDR_TYPE_CACHE) {
				// Delete Cache Header
				avmf_unlink_header(r, c);
				goto try_continue_next_after_corrupt;
				break;
			}
			if (c->signature != AVMF_SIGNATURE || c->version != AVMF_VERSION) {
				// Delete Cache Header
				avmf_unlink_header(r, c);
				goto try_continue_next_after_corrupt;
				break;
			}

			if (min_virt > 0 && c->virt_addr < min_virt) continue;
			if (max_virt > 0 && c->virt_addr + size > max_virt) continue;

			if (c->size == size && (c->virt_addr % align) == 0) {
				// Perfect Match
				avmf_unlink_header(r, c);

				c->type = AVMF_HDR_TYPE_ALLOC;
				if (!avmf_link_header(r, c)) {
					continue;
				}

				c->used = AOS_TRUE;
				return c;
			} else if (c->size > size) {
				if ((c->virt_addr % align) == 0) {
					// Use from start of cache is perfect
					avmf_header_t* hdr = avmf_alloc_ihdr();
					if (!hdr) continue;

					memset(hdr, 0, sizeof(avmf_header_t));
					hdr->signature = AVMF_SIGNATURE;
					hdr->version = AVMF_VERSION;
					hdr->type = AVMF_HDR_TYPE_ALLOC;
					hdr->virt_addr = c->virt_addr;
					hdr->phys_addr = c->phys_addr;
					hdr->size = size;
					hdr->used = AOS_TRUE;

					if (!avmf_link_header(r, hdr)) {
						avmf_free_ihdr(hdr);
						continue;
					}

					c->size -= hdr->size;
					c->virt_addr += size;
					c->phys_addr += size;

					if (c->size < 0x200) { // caches are only stored from size of 512 bytes
						avmf_range_t* range = avmf_alloc_irange();
						if (range) {
							range->base = c->virt_addr;
							range->size = c->size;

							if (!avmf_free_list_append(r, range)) {
								avmf_free_irange(range);
							} else {
								r->allocated_bytes -= c->size;
								avmf_unlink_header(r, c);
							}
						}
					}

					return hdr;
				}
			}
		}
	}

	if (r->free_count > 0 && r->free_list) {
        avmf_range_t* prev = NULL;
        avmf_range_t* cur = r->free_list;

        for (uint64_t i = 0; i < r->free_count && cur; i++) {
            uint64_t range_end;

            if (cur->size == 0) goto next_free;
            range_end = cur->base + cur->size;

            uint64_t alloc_base = cur->base;
            if (alloc_base < min_virt) alloc_base = min_virt;

            if (alloc_base % align) {
                uint64_t rem = alloc_base % align;
                uint64_t adjustment = align - rem;
                alloc_base += adjustment;
            }

            if (alloc_base < cur->base) goto next_free;
            if (alloc_base > range_end) goto next_free;
            if (size > range_end - alloc_base) goto next_free;

            if (max_virt && (alloc_base > max_virt || size > max_virt - alloc_base)) goto next_free;

            avmf_header_t* hdr = avmf_alloc_ihdr();
            if (!hdr) return NULL;
            memset(hdr, 0, sizeof(*hdr));

            hdr->signature = AVMF_SIGNATURE;
            hdr->version = AVMF_VERSION;
            hdr->type = AVMF_HDR_TYPE_ALLOC;
            hdr->virt_addr = alloc_base;
            hdr->size = size;
            hdr->used = AOS_TRUE;

            uint64_t prefix = alloc_base - cur->base;
            uint64_t suffix = range_end - (alloc_base + size);

            if (prefix == 0 && suffix == 0) {
                if (prev) prev->next = cur->next;
                else r->free_list = cur->next;

                if (r->free_list_end == cur) r->free_list_end = prev;
                if (r->free_count > 0) r->free_count--;

                avmf_free_irange(cur);
            } else if (prefix == 0) {
                cur->base = alloc_base + size;
                cur->size = suffix;
            } else if (suffix == 0) {
                cur->size = prefix;
            } else {
                avmf_range_t* suffix_range = avmf_alloc_irange();

                if (!suffix_range) {
                    avmf_free_ihdr(hdr);
                    return NULL;
                }

                suffix_range->base = alloc_base + size;
                suffix_range->size = suffix;
                suffix_range->next = cur->next;

                cur->size = prefix;
                cur->next = suffix_range;

                if (r->free_list_end == cur) r->free_list_end = suffix_range;
                r->free_count++;
            }

            if (!avmf_link_header(r, hdr)) {
                avmf_range_t* rollback = avmf_alloc_irange();

                if (rollback) {
                    rollback->base = hdr->virt_addr;
                    rollback->size = hdr->size;

                    if (!avmf_free_list_append(r, rollback)) avmf_free_irange(rollback);
                }

                avmf_free_ihdr(hdr);
                return NULL;
            }

			r->allocated_bytes += hdr->size;
            return hdr;
		}

        next_free: {
            prev = cur;
            cur = cur->next;
        }
    }

	return NULL;
}

static void avmf_region_free(avmf_region_header_t* r, uint64_t base, uint64_t size) {
	if (r->signature != AVMF_SIGNATURE) return;
    if (r->version != AVMF_VERSION) return;
    if (r->limit < 1 || r->limit <= r->base) return;
	
	size = align4k(size);
    if (base > UINT64_MAX - size) return;

    uint64_t end = base + size;
    if (r->alloc_count == 0 || !r->alloc_list) return;

    avmf_header_t* c = r->alloc_list;
    for (uint64_t i = 0; i < r->alloc_count; i++) {
        if (!c) return;
        avmf_header_t* n = c->next;
        if (c->type == AVMF_HDR_TYPE_CORRUPT) {
			try_continue_next_after_corrupt: {
				if (!n) {
					break;
				}
				
				if (n->type != AVMF_HDR_TYPE_ALLOC) {
					break;
				} else {
					c = n;
					continue;
				}
			}
		}

        if (c->signature != AVMF_SIGNATURE || c->version != AVMF_VERSION) {
            avmf_unlink_header(r, c);
			goto try_continue_next_after_corrupt;
        }

        if (c->type != AVMF_HDR_TYPE_ALLOC) {
            avmf_unlink_header(r, c);
			goto try_continue_next_after_corrupt;
        }

        if (c->virt_addr != base || c->size != size) {
            c = n;
            continue;
        }

        if (c->size > 0x200) { // TODO: Make condition better
            // Go to Cache
            avmf_unlink_header(r, c);
            c->type = AVMF_HDR_TYPE_CACHE;
            if (!avmf_link_header(r, c)) {
                goto add_to_free_list;
            }

            c->type = AVMF_HDR_TYPE_CACHE;
            c->used = AOS_FALSE;
            return;
        }

		add_to_free_list: {
			avmf_range_t* range = avmf_alloc_irange();
			if (!range) return;

			range->base = c->virt_addr;
			range->size = c->size;
			range->next = NULL;

			if (!avmf_free_list_append(r, range)) {
				avmf_free_irange(range);
				return;
			}
			avmf_unlink_header(r, c);
			c->used = AOS_FALSE;
			r->allocated_bytes -= c->size;

			return;
		}
    }
}

uint64_t avmf_alloc_phys_contiguous(uint64_t size) {
	if (size == 0) return 0;

    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    uint64_t sz = align4k(size);
    uint64_t pages_needed = sz / PAGE_SIZE;

    for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

        uint64_t start_page = r->base / PAGE_SIZE;
        uint64_t end_page = r->limit / PAGE_SIZE;
        uint64_t consecutive = 0;
        uint64_t first_page = 0;

        for (uint64_t p = start_page; p < end_page; p++) {
            if (!bitmap_test(p)) {
                if (consecutive == 0) first_page = p;
                consecutive++;

                if (consecutive == pages_needed) {
                    for (uint64_t j = first_page; j < first_page + pages_needed; j++) {
                        bitmap_set(j);
                    }
                    spin_unlock_irqrestore(&avmf_lock, rflags);

					uint64_t r_rflags = spin_lock_irqsave(&r->lock);
					r->allocated_bytes += pages_needed * PAGE_SIZE;
					spin_unlock_irqrestore(&r->lock, r_rflags);
					
                    return first_page * PAGE_SIZE;
                }
            } else {
                consecutive = 0;
            }
        }
    }

    serial_printf("[AVMF] Out of physical memory (Range: 0x%llx bytes)\n", sz);
    spin_unlock_irqrestore(&avmf_lock, rflags);
    return 0;
}

void avmf_free_phys_contiguous(uint64_t phys, uint64_t size) {
	if (size == 0) return;

    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    uint64_t sz = align4k(size);
    uint64_t pages_needed = sz / PAGE_SIZE;

	uint64_t first_page = ALIGN_DOWN(phys, PAGE_SIZE) / PAGE_SIZE;

    for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE) continue;
		if (r->version != AVMF_VERSION) continue;
		if (r->limit < 1 || r->limit <= r->base) continue;

        uint64_t start_page = r->base / PAGE_SIZE;
        uint64_t end_page = r->limit / PAGE_SIZE;

		if (first_page < start_page || first_page >= end_page) continue;
		if (pages_needed > end_page - first_page) continue;

		aos_bool found = AOS_TRUE;
        for (uint64_t p = first_page; p < first_page + pages_needed; p++) {
            if (!bitmap_test(p)) {
				found = AOS_FALSE;
				break;
            }
        }
		if (!found) continue;

		for (uint64_t p = first_page; p < first_page + pages_needed; p++) {
			bitmap_clear(p);
		}
		spin_unlock_irqrestore(&avmf_lock, rflags);

		uint64_t r_rflags = spin_lock_irqsave(&r->lock);
		r->allocated_bytes = r->allocated_bytes >= pages_needed * PAGE_SIZE ? r->allocated_bytes - pages_needed * PAGE_SIZE : 0;
		spin_unlock_irqrestore(&r->lock, r_rflags);
		return;
    }
    spin_unlock_irqrestore(&avmf_lock, rflags);
    return;
}

static avmf_header_t* avmf_alloc_hdr_internal(uint64_t size, MemoryAllocType type, avmf_region_header_t** out_region) {
    if (size < 1) return NULL;

    uint64_t min_virt = 0;
    uint64_t max_virt = 0;

    switch (type) {
        case MALLOC_TYPE_USER:
            min_virt = AOS_USER_SPACE_BASE;
            max_virt = AOS_DIRECT_MAP_BASE;
            break;

        case MALLOC_TYPE_KERNEL:
            min_virt = AOS_KERNEL_SPACE_BASE;
            max_virt = AOS_DRIVER_SPACE_BASE;
            break;

        case MALLOC_TYPE_DRIVER:
            min_virt = AOS_DRIVER_SPACE_BASE;
            max_virt = AOS_SENSITIVE_SPACE_BASE;
            break;

        case MALLOC_TYPE_SENSITIVE:
            min_virt = AOS_SENSITIVE_SPACE_BASE;
            max_virt = UINT64_MAX;
            break;

        default:
            serial_print("[AVMF] Invalid VMemory Allocation Type!\n");
            return NULL;
    }

	avmf_region_header_t* r = avmf_find_region(min_virt, max_virt, size);
	if (out_region) *out_region = r;
	if (!r) {
        serial_print("[AVMF] Could not find suitable region!\n");
        return NULL;
    }
	
	avmf_header_t* hdr = avmf_region_alloc(r, size, PAGE_SIZE, min_virt, max_virt);
    if (!hdr) return NULL;
    return hdr;
}

uint64_t avmf_alloc_virt(uint64_t size, MemoryAllocType type) {
    avmf_header_t* h = avmf_alloc_hdr_internal(align4k(size), type, NULL);
	if (!h) return 0;
	return h->virt_addr;
}

uint64_t avmf_alloc(uint64_t size, MemoryAllocType type, uint32_t flags, uint64_t* phys_out) {
	if (phys_out != NULL) *phys_out = 0;
	uint64_t true_size = align4k(size);

	avmf_region_header_t* r = NULL;
    avmf_header_t* hdr = avmf_alloc_hdr_internal(true_size, type, &r);
    if (!hdr || !r) return 0;

	uint32_t f = avmf_convert_flags_to_pager_flags(flags);
	if (hdr->phys_addr > 0) {
    	if (hdr->type != AVMF_HDR_TYPE_CACHE) {
			pager_map_range((uint64_t)hdr->virt_addr, hdr->phys_addr, hdr->size, f);
		}
		if (phys_out != NULL) *phys_out = hdr->phys_addr;

		return hdr->virt_addr;
	}

    uint64_t phys = avmf_alloc_phys_contiguous(hdr->size);
    if (!phys) {
		serial_printf("[AVMF] Unable to retrieve physical address of VMemory Allocation for Size: 0x%llx bytes\n", hdr->size);
		avmf_region_free(r, hdr->virt_addr, hdr->size);
		return 0;
	}

    pager_map_range((uint64_t)hdr->virt_addr, phys, hdr->size, f);
    if (phys_out != NULL) *phys_out = phys;
    return hdr->virt_addr;
}

void avmf_free(uint64_t virt) {
	if (!virt) return; // AVMF cannot allocate < 1MB

	avmf_region_header_t* r = avmf_find_region(virt, UINT64_MAX, virt + 1);
	if (!r) return;

    avmf_header_t* hdr = r->alloc_list;
	aos_bool found = AOS_FALSE;
    while (hdr) {
		if (hdr->type != AVMF_HDR_TYPE_ALLOC) continue;
		if (hdr->signature != AVMF_SIGNATURE) continue;
		if (hdr->version != AVMF_VERSION) continue;

        if (virt >= hdr->virt_addr && virt < hdr->virt_addr + hdr->size) {
			found = AOS_TRUE;
            break;
        }
        hdr = hdr->next;
    }
    
	if (!found) return;

    uint64_t rflags = spin_lock_irqsave(&avmf_lock);

    uint64_t phys = hdr->phys_addr;
    uint64_t page_idx = phys / PAGE_SIZE;
    uint64_t pages = hdr->size / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++) {
        bitmap_clear(page_idx + i);
		pager_unmap(hdr->virt_addr + (PAGE_SIZE * i));
    }

	spin_unlock_irqrestore(&avmf_lock, rflags);
	avmf_region_free(r, hdr->virt_addr, hdr->size);
}

void avmf_init(uint64_t* base_phys, uint64_t* limit_phys, uint8_t entries) {
    uint64_t rflags = spin_lock_irqsave(&avmf_lock);
    uint64_t ent = entries <= AVMF_STATIC_SIZE ? entries : AVMF_STATIC_SIZE;
    for (uint64_t i = 0; i < ent; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		avmf_region_init(r, base_phys[i], limit_phys[i]);
		physical_region_count++;
    }

	avmf_region_init(&regions[0], AOS_USER_SPACE_BASE, AOS_USER_SPACE_BASE_END);
	avmf_region_init(&regions[1], AOS_KERNEL_SPACE_BASE, AOS_DRIVER_SPACE_BASE);
	avmf_region_init(&regions[2], AOS_DRIVER_SPACE_BASE, AOS_SENSITIVE_SPACE_BASE);
	avmf_region_init(&regions[3], AOS_SENSITIVE_SPACE_BASE, AOS_SENSITIVE_SPACE_BASE_END);
	region_count = 4;

    spin_unlock_irqrestore(&avmf_lock, rflags);
}

avmf_header_t* avmf_find(uint64_t virt) {
    avmf_region_header_t* r = avmf_find_region(virt, UINT64_MAX, virt + 1);
	if (!r) return NULL;
    avmf_header_t* cur = r->alloc_list;
    while (cur) {
		if (cur->type != AVMF_HDR_TYPE_ALLOC) continue;
		if (cur->signature != AVMF_SIGNATURE) continue;
		if (cur->version != AVMF_VERSION) continue;

        if (virt >= cur->virt_addr && virt < cur->virt_addr + cur->size) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static void avmf_print_info_region(aos_bool vmem, struct VMemDesign* design, avmf_region_header_t* region, const char* name) {
	aos_bool vmem_out = vmem && design;

	if (name) {
		if (vmem_out) vmem_printf(design, "%s: ", name);
		else serial_printf("%s: ", name);
	} else {
		if (vmem_out) vmem_print(design, "Unnamed Region: ");
		else serial_print("Unnamed Region: ");
	}

	if (!region) {
		if (vmem_out) vmem_print(design, "Null Region\n");
		else serial_print("Null Region\n");
		return;
	}

	if (vmem_out) vmem_printf(design, "\n\tBase Address: 0x%llX\n", region->base);
	else serial_printf("\n\tBase Address: 0x%llX\n", region->base);

	double pretty_size = 0.0;
	const char* pretty_unit = kbeautify_memory_size(region->limit, &pretty_size);

	if (vmem_out) vmem_printf(design, "\tSize: %.2lf %s\n", pretty_size, pretty_unit);
	else serial_printf("\tSize: %.2lf %s\n", pretty_size, pretty_unit);

	pretty_size = 0.0;
	pretty_unit = kbeautify_memory_size(region->allocated_bytes, &pretty_size);

	if (vmem_out) vmem_printf(design, "\tAllocated: %.2lf %s\n", pretty_size, pretty_unit);
	else serial_printf("\tAllocated: %.2lf %s\n", pretty_size, pretty_unit);

	pretty_size = 0.0;
	pretty_unit = kbeautify_memory_size(region->limit - region->allocated_bytes, &pretty_size);

	if (vmem_out) vmem_printf(design, "\tFree: %.2lf %s\n", pretty_size, pretty_unit);
	else serial_printf("\tFree: %.2lf %s\n", pretty_size, pretty_unit);
}

void avmf_print_info(aos_bool vmem, struct VMemDesign* design) {
	aos_bool vmem_out = vmem && design;

	if (vmem_out) vmem_print(design, "Physical:\n");
	else serial_print("Physical:\n");

	for (uint64_t i = 0; i < physical_region_count; i++) {
		avmf_region_header_t* r = &physical_regions[i];
		if (r->signature != AVMF_SIGNATURE || r->version != AVMF_VERSION) continue;
		if (r->limit < r->allocated_bytes || r->limit == 0) continue;

		avmf_print_info_region(vmem, design, r, "Physical Region");
	}

	if (vmem_out) vmem_print(design, "Virtual:\n");
	else serial_print("Virtual:\n");

	for (uint64_t i = 0; i < region_count; i++) {
		avmf_region_header_t* r = &regions[i];
		if (r->signature != AVMF_SIGNATURE || r->version != AVMF_VERSION) continue;
		if (r->limit < r->allocated_bytes || r->limit == 0) continue;

		const char* name = NULL;
		switch (i) {
			case 0: name = "User Space"; break;
			case 1: name = "Kernel Space"; break;
			case 2: name = "Driver Space"; break;
			case 3: name = "Sensitive Space"; break;
			default: break;
		}

		avmf_print_info_region(vmem, design, r, name);
	}
}
