#include <inttypes.h>
#include <aos_bitmap_font.h>

#include <inc/mm/avmf.h>

#include <inc/drivers/io/io.h>
#include <inc/core/kfuncs.h>
#include <inc/core/fs.h>
#include <inc/extra/aosbf.h>

#define AOSBF_ATLAS_MAX_HEIGHT 134217728 // 128MB

typedef struct {
	uint64_t atlas_phys;
	uint32_t* atlas;

	uint64_t atlas_size;
	uint64_t atlas_width;
	uint64_t atlas_height;
	uint64_t atlas_row_height;
	
	uint64_t atlas_x;
	uint64_t atlas_y;
} aosbf_atlas_data;

static aos_bool aosbf_convert_glyph_to_rgba8(aosbf_atlas_data* atlas, uint64_t* atlas_x, uint64_t* atlas_y, const uint8_t* bitmap, uint64_t bitmap_size, uint32_t width, uint32_t height, uint32_t pitch, enum aos_bitmap_font_format format) {
    if (!atlas || !atlas_x || !atlas_y || !bitmap) {
		serial_print("[EXTRA:AOSBF] Invalid Arguments Provided!\n");
		return AOS_FALSE;
	}
	if (width == 0 || height == 0) {
		serial_print("[EXTRA:AOSBF] Invalid Arguments Provided!\n");
		return AOS_FALSE;
	}

    const uint64_t INITIAL_WIDTH = 512;
    const uint64_t INITIAL_HEIGHT = 512;
    const uint64_t PADDING = 1;

    if (atlas->atlas == NULL) {
        atlas->atlas_width = INITIAL_WIDTH;
        atlas->atlas_height = INITIAL_HEIGHT;

        uint64_t pixels;
        if (atlas->atlas_width > UINT64_MAX / atlas->atlas_height) {
			serial_print("[EXTRA:AOSBF] Overflow of Atlas Width!\n");
			return AOS_FALSE;
		}
        
		pixels = atlas->atlas_width * atlas->atlas_height;
        if (pixels > UINT64_MAX / sizeof(uint32_t)) {
			serial_print("[EXTRA:AOSBF] Overflow of Atlas Size!\n");
			return AOS_FALSE;
		}

        atlas->atlas_size = pixels * sizeof(uint32_t);
        atlas->atlas = (uint32_t*)avmf_alloc(atlas->atlas_size, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, &atlas->atlas_phys);
        if (!atlas->atlas || !atlas->atlas_phys) {
			serial_print("[EXTRA:AOSBF] Failed to allocate Atlas!\n");
			return AOS_FALSE;
		}

        memset(atlas->atlas, 0, atlas->atlas_size);

        atlas->atlas_x = 0;
        atlas->atlas_y = 0;
    }

	if ((uint64_t)width > atlas->atlas_width) {
		serial_print("[EXTRA:AOSBF] Glyph is wider than atlas!\n");
		return AOS_FALSE;
	}

	if (atlas->atlas_x > UINT64_MAX - (uint64_t)width - PADDING) {
		serial_print("[EXTRA:AOSBF] Overflow is Atlas X!\n");
		return AOS_FALSE;
	}
    if (atlas->atlas_x + width + PADDING > atlas->atlas_width) {
        atlas->atlas_x = 0;
        
		if (atlas->atlas_row_height > UINT64_MAX - PADDING) {
			serial_print("[EXTRA:AOSBF] Overflow of Atlas Row Height!\n");
			return AOS_FALSE;
		}

        atlas->atlas_y += atlas->atlas_row_height + PADDING;
        atlas->atlas_row_height = 0;
    }

    uint64_t required_height;
    if (atlas->atlas_y > UINT64_MAX - height) {
		serial_print("[EXTRA:AOSBF] Overflow is Atlas Y!\n");
		return AOS_FALSE;
	}

    required_height = atlas->atlas_y + height;
    if (required_height > atlas->atlas_height) {
        uint64_t new_height = atlas->atlas_height;
		if (required_height > UINT64_MAX - 511) {
			new_height = required_height;
		} else {
			new_height = (required_height + 511) & ~UINT64_C(511);
		}

        if (new_height < required_height) {
			serial_print("[EXTRA:AOSBF] Requirement is too large to handle!\n");
			return AOS_FALSE;
		}
        if (new_height > UINT64_MAX / atlas->atlas_width) {
			serial_print("[EXTRA:AOSBF] Overflow of Required Atlas Height!\n");
			return AOS_FALSE;
		}
		
        uint64_t new_pixels = atlas->atlas_width * new_height;
        if (new_pixels > UINT64_MAX / sizeof(uint32_t)) {
			serial_print("[EXTRA:AOSBF] Overflow of required Atlas size!\n");
			return AOS_FALSE;
		}

        uint64_t new_size = new_pixels * sizeof(uint32_t);
		if (new_size > AOSBF_ATLAS_MAX_HEIGHT) {
			serial_print("[EXTRA:AOSBF] Atlas exceeded maximum height!\n");
			return AOS_FALSE;
		}

		uint64_t new_atlas_phys = 0;
        uint32_t* new_atlas = (uint32_t*)avmf_alloc(new_size, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, &new_atlas_phys);
        if (!new_atlas || !new_atlas_phys) {
			serial_print("[EXTRA:AOSBF] Could not reallocate Atlas\n");
			return AOS_FALSE;
		}

		double pretty_size = (double)new_size;
		const char* pretty_unit = kbeautify_memory_size(new_size, &pretty_size);
		serial_printf("[EXTRA:AOSBF] Resized Atlas to (0x%llX, 0x%llX) [%.02llf %s]\n", atlas->atlas_width, new_height, pretty_size, pretty_unit);

        memset(new_atlas, 0, new_size);

        for (uint64_t y = 0; y < atlas->atlas_height; y++) {
            memcpy(new_atlas + y * atlas->atlas_width, atlas->atlas + y * atlas->atlas_width, atlas->atlas_width * sizeof(uint32_t));
        }

        avmf_free((uint64_t)atlas->atlas);

		atlas->atlas_phys = new_atlas_phys;
        atlas->atlas = new_atlas;
        atlas->atlas_height = new_height;
        atlas->atlas_size = new_size;
    }

    if (pitch == 0) {
		serial_print("[EXTRA:AOSBF] Invalid Glyph Pitch!\n");
		return AOS_FALSE;
	}
    if ((uint64_t)pitch * height > bitmap_size) {
		serial_print("[EXTRA:AOSBF] Invalid Glyph Size!\n");
		return AOS_FALSE;
	}

    uint64_t minimum_pitch;
    switch (format) {
        case AOSBF_FORMAT_MONO:
            minimum_pitch = ((uint64_t)width + 7) / 8;
            break;

        case AOSBF_FORMAT_GRAY2:
            minimum_pitch = ((uint64_t)width + 3) / 4;
            break;

        case AOSBF_FORMAT_GRAY4:
            minimum_pitch = ((uint64_t)width + 1) / 2;
            break;

        case AOSBF_FORMAT_A8:
            minimum_pitch = width;
            break;

        case AOSBF_FORMAT_RGB:
            minimum_pitch = (uint64_t)width * 3;
            break;

        case AOSBF_FORMAT_RGBA:
            minimum_pitch = (uint64_t)width * 4;
            break;

        default:
			serial_print("[EXTRA:AOSBF] Invalid Glyph Format!\n");
            return AOS_FALSE;
    }

    if (pitch < minimum_pitch) {
		serial_print("[EXTRA:AOSBF] Invalid Glyph Pitch as its less than minimum!\n");
		return AOS_FALSE;
	}

    uint64_t dst_x = atlas->atlas_x;
    uint64_t dst_y = atlas->atlas_y;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src_row = bitmap + ((uint64_t)y * pitch);

        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 255;
            uint8_t g = 255;
            uint8_t b = 255;
            uint8_t a = 255;

            switch (format) {
                case AOSBF_FORMAT_MONO: {
                    uint8_t byte = src_row[x >> 3];
                    uint8_t bit = 7 - (x & 7);
                    a = (byte & (1u << bit)) ? 255 : 0;
                    break;
                }

                case AOSBF_FORMAT_GRAY2: {
                    uint8_t byte = src_row[x >> 2];
                    uint8_t shift = 6 - ((x & 3) * 2);
                    uint8_t value = (byte >> shift) & 0x3;

                    a = value * 85;
                    break;
                }

                case AOSBF_FORMAT_GRAY4: {
                    uint8_t byte = src_row[x >> 1];
                    uint8_t value;

                    if (x & 1) value = byte & 0x0F;
                    else value = byte >> 4;

                    a = value * 17;
                    break;
                }

                case AOSBF_FORMAT_A8: {
                    a = src_row[x];
                    break;
				}

                case AOSBF_FORMAT_RGB: {
                    const uint8_t* p = src_row + ((uint64_t)x * 3);

                    r = p[0];
                    g = p[1];
                    b = p[2];
                    a = 255;

                    break;
                }

                case AOSBF_FORMAT_RGBA: {
                    const uint8_t* p = src_row + ((uint64_t)x * 4);

                    r = p[0];
                    g = p[1];
                    b = p[2];
                    a = p[3];

                    break;
                }

                default: {
					serial_print("[EXTRA:AOSBF] Invalid Glyph Format!\n");
					return AOS_FALSE;
				}
            }

            uint64_t dst_index = ((dst_y + y) * atlas->atlas_width) + (dst_x + x);
            (atlas->atlas)[dst_index] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8)  | ((uint32_t)r);
		}
    }

    atlas->atlas_x = dst_x;
    atlas->atlas_y = dst_y;
	*atlas_x = dst_x;
	*atlas_y = dst_y;

    atlas->atlas_x += width + PADDING;
    if (height > atlas->atlas_row_height) atlas->atlas_row_height = height;

    return AOS_TRUE;
}

aos_bool aosbf_to_pyrion_font(uint8_t* data, uint64_t size, struct pyrion_font* out) {	
	if (!data || !out) {
		serial_print("[EXTRA:AOSBF] Invalid Arguments Provided!\n");
		return AOS_FALSE;
	}
	if (size < sizeof(struct aos_bitmap_font_hdr)) {
		serial_print("[EXTRA:AOSBF] Data is smaller than required minimum!\n");
		return AOS_FALSE;
	}

	memset(out, 0, sizeof(*out));
	out->valid = AOS_FALSE;

	struct aos_bitmap_font_hdr* hdr = (struct aos_bitmap_font_hdr*)data;
	if (memcmp(hdr->magic, AOS_BITMAP_FONT_MAGIC, AOS_BITMAP_FONT_MAGIC_LEN) != 0) {
		serial_print("[EXTRA:AOSBF] Invalid Magic!\n");
		return AOS_FALSE;
	}
	if (hdr->version != AOS_BITMAP_FONT_VERSION || hdr->revision != AOS_BITMAP_FONT_REVISION) {
		serial_print("[EXTRA:AOSBF] Invalid Version/Revision!\n");
		return AOS_FALSE;
	}

	if (hdr->string_table >= size || hdr->atlas >= size) {
		serial_print("[EXTRA:AOSBF] Invalid String Table offset!\n");
		return AOS_FALSE;
	}
	if (hdr->total_w < 1 || hdr->total_h < 1) {
		serial_print("[EXTRA:AOSBF] No Data in Font!\n");
		return AOS_FALSE;
	}

	// char* str_tab = (char*)(data + hdr->string_table);

	// Basic Info
	out->base_font_size = hdr->font_size;
	out->font_size = hdr->font_size;
	out->line_gap = hdr->line_gap;
	out->line_height = hdr->line_height;
	out->ascent = hdr->ascent;
	out->descent = hdr->descent;

	// Glyphs (Validation)
	out->glyph_count = hdr->glyph_count;

	for (uint64_t i = 0; i < hdr->glyph_count; i++) {
		if (hdr->glyphs > size) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Array Offset!\n");
			return AOS_FALSE;
		}
		if (i > (size - hdr->glyphs) / sizeof(struct aos_bitmap_font_glyph)) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Array Offset!\n");
			return AOS_FALSE;
		}

		uint64_t off = hdr->glyphs + (i * sizeof(struct aos_bitmap_font_glyph));
		if (off > size) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Array Offset!\n");
			return AOS_FALSE;
		}
		
		struct aos_bitmap_font_glyph* g = (struct aos_bitmap_font_glyph*)(data + off);
		if (!g->valid || g->width == 0 || g->height == 0) {
			out->glyph_count--;
			continue;
		}
	
		uint64_t source_offset;
		uint64_t atlas_end;

		if (g->atlas_y == UINT32_MAX) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Bitmap Y!\n");
			return AOS_FALSE;
		}
		if (g->atlas_x != 0 && (uint64_t)((uint64_t)g->atlas_y + 1) > UINT64_MAX / (uint64_t)g->atlas_x) return AOS_FALSE;

		uint64_t bitmap_size;
		if ((uint64_t)g->pitch > UINT64_MAX / g->height) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Bitmap Pitch!\n");
			return AOS_FALSE;
		}
		bitmap_size = (uint64_t)g->pitch * g->height;

		source_offset = (uint64_t)((uint64_t)g->atlas_y + 1) * (uint64_t)g->atlas_x;
		if (source_offset > UINT64_MAX - bitmap_size) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Bitmap Coordinates!\n");
			return AOS_FALSE;
		}

		atlas_end = source_offset + bitmap_size;

		if (hdr->atlas > size || atlas_end > size - hdr->atlas) {
			serial_print("[EXTRA:AOSBF] Invalid Glyph Bitmap Coordinates!\n");
			return AOS_FALSE;
		}
	}
	if (out->glyph_count > UINT64_MAX / sizeof(struct pyrion_glyph)) {
		serial_print("[EXTRA:AOSBF] Corrupt Glyph Count!\n");
		return AOS_FALSE;
	}
	if (out->glyph_count < 1) {
		serial_print("[EXTRA:AOSBF] No Valid Glyphs!\n");
		return AOS_FALSE;
	}

	// Glyphs (Actual Allocation and setup)
	struct pyrion_glyph* glyphs = (struct pyrion_glyph*)avmf_alloc(out->glyph_count * sizeof(struct pyrion_glyph), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!glyphs) {
		serial_print("[EXTRA:AOSBF] Failed to allocate Pyrion Glyphs!\n");
		return AOS_FALSE;
	}
	memset(glyphs, 0, out->glyph_count * sizeof(struct pyrion_glyph));

	aosbf_atlas_data atlas = {0};

	uint64_t gidx = 0;
	for (uint64_t i = 0; i < hdr->glyph_count; i++) {
		uint64_t off = hdr->glyphs + (i * sizeof(struct aos_bitmap_font_glyph));

		struct aos_bitmap_font_glyph* g = (struct aos_bitmap_font_glyph*)(data + off);
		if (!g->valid || g->width == 0 || g->height == 0) continue;

		uint64_t bitmap_size;
		bitmap_size = (uint64_t)g->pitch * g->height;

		struct pyrion_glyph* og = &glyphs[gidx++];
		
		uint64_t atlas_x = 0;
		uint64_t atlas_y = 0;

		uint64_t source_offset = (uint64_t)((uint64_t)g->atlas_y + 1) * (uint64_t)g->atlas_x;

		// Convert AOSBF Atlas to RGBA8 Pyrion ATLAS
		if (!aosbf_convert_glyph_to_rgba8(&atlas, &atlas_x, &atlas_y, (const uint8_t*)(data + hdr->atlas + source_offset), bitmap_size, g->width, g->height, g->pitch, g->format)) {
			if (atlas.atlas) avmf_free((uint64_t)atlas.atlas);
			atlas.atlas = NULL;

			if (glyphs) avmf_free((uint64_t)glyphs);
			glyphs = NULL;

			return AOS_FALSE;
		}

		og->codepoint = g->codepoint;
		
		og->atlas_x = atlas_x;
		og->atlas_y = atlas_y;
		
		og->width = g->width;
		og->height = g->height;
		
		og->bearing_x = g->bearing_x;
		og->bearing_y = g->bearing_y;
		
		og->advance_x = g->advance_x;
		og->advance_y = g->advance_y;

		og->valid = AOS_TRUE;

		if (g->is_fallback) {
			out->fallback_glyph = *og;
		}
	}

	out->atlas_phys = atlas.atlas_phys;
	out->atlas = (uint8_t*)atlas.atlas;
	out->w = (uint32_t)atlas.atlas_width;
	out->h = (uint32_t)atlas.atlas_height;
	
	out->glyphs = glyphs;
	out->valid = AOS_TRUE;

	return AOS_TRUE;
}

aos_bool aosbf_file_to_pyrion_font(const char* path, struct pyrion_font* out) {
	if (!path || !out) {
		serial_print("[EXTRA:AOSBF] Invalid Arguments Provided!\n");
		return AOS_FALSE;
	}

	struct aos_file* f = fs_open(path);
	if (!f) {
		serial_printf("[EXTRA:AOSBF] Could not open File \"%s\"!\n", path);
		return AOS_FALSE;
	}

	if (!f->seek(f, 0, FS_SEEK_MODE_END)) {
		serial_printf("[EXTRA:AOSBF] Could not seek in File \"%s\"!\n", path);
		fs_close(f);
		return AOS_FALSE;
	}

	uint64_t size = f->tell(f);
	if (size < 1) {
		serial_printf("[EXTRA:AOSBF] File \"%s\" is Empty!\n", path);
		fs_close(f);
		return AOS_FALSE;
	}

	uint8_t* data = (uint8_t*)avmf_alloc(size, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!data) {
		fs_close(f);
		return AOS_FALSE;
	}

	if (!f->seek(f, 0, FS_SEEK_MODE_SET)) {
		serial_printf("[EXTRA:AOSBF] Could not seek in File \"%s\"!\n", path);
		fs_close(f);
		avmf_free((uint64_t)data);
		return AOS_FALSE;
	}

	if (!f->read(f, size, data)) {
		serial_printf("[EXTRA:AOSBF] Could not read File \"%s\"!\n", path);
		fs_close(f);
		avmf_free((uint64_t)data);
		return AOS_FALSE;
	}

	fs_close(f);

	aos_bool ret = aosbf_to_pyrion_font(data, size, out);
	avmf_free((uint64_t)data);

	return ret;
}
