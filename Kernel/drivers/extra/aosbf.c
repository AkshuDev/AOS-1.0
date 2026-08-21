#include <inttypes.h>
#include <aos_bitmap_font.h>

#include <inc/mm/avmf.h>

#include <inc/core/kfuncs.h>
#include <inc/extra/aosbf.h>

static aos_bool aosbf_convert_glyph_to_rgba8(uint32_t** atlas, uint64_t* atlas_size, uint64_t* atlas_width, uint64_t* atlas_height, uint64_t* atlas_row_height, uint64_t* atlas_x, uint64_t* atlas_y, const uint8_t* bitmap, uint64_t bitmap_size, uint32_t width, uint32_t height, uint32_t pitch, enum aos_bitmap_font_format format) {
    if (
		!atlas ||
        !atlas_size ||
        !atlas_width ||
        !atlas_height ||
        !atlas_x ||
        !atlas_y ||
        !bitmap ||
		!atlas_row_height ||
        width == 0 ||
        height == 0 
	) return AOS_FALSE;

    const uint64_t INITIAL_WIDTH = 512;
    const uint64_t INITIAL_HEIGHT = 512;
    const uint64_t PADDING = 1;

    if (*atlas == NULL) {
        *atlas_width = INITIAL_WIDTH;
        *atlas_height = INITIAL_HEIGHT;

        uint64_t pixels;
        if (*atlas_width > UINT64_MAX / *atlas_height) return AOS_FALSE;
        
		pixels = *atlas_width * *atlas_height;
        if (pixels > UINT64_MAX / sizeof(uint32_t)) return AOS_FALSE;

        *atlas_size = pixels * sizeof(uint32_t);
        *atlas = (uint32_t*)avmf_alloc(*atlas_size, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
        if (!*atlas) return AOS_FALSE;

        memset(*atlas, 0, *atlas_size);

        *atlas_x = 0;
        *atlas_y = 0;
    }

    if (*atlas_x + width + PADDING > *atlas_width) {
        *atlas_x = 0;
        
		if (*atlas_row_height > UINT64_MAX - PADDING) return AOS_FALSE;

        *atlas_y += *atlas_row_height + PADDING;
        *atlas_row_height = 0;
    }

    uint64_t required_height;
    if (*atlas_y > UINT64_MAX - height) return AOS_FALSE;

    required_height = *atlas_y + height;
    if (required_height > *atlas_height) {
        uint64_t new_height = *atlas_height;

        while (new_height < required_height) {
            if (new_height > UINT64_MAX / 2) {
                new_height = required_height;
                break;
            }

            new_height *= 2;
        }

        if (new_height < required_height) return AOS_FALSE;
        if (new_height > UINT64_MAX / *atlas_width) return AOS_FALSE;

        uint64_t new_pixels = *atlas_width * new_height;
        if (new_pixels > UINT64_MAX / sizeof(uint32_t)) return AOS_FALSE;

        uint64_t new_size = new_pixels * sizeof(uint32_t);
        uint32_t* new_atlas = (uint32_t*)avmf_alloc(new_size, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
        if (!new_atlas) return AOS_FALSE;

        memset(new_atlas, 0, new_size);

        for (uint64_t y = 0; y < *atlas_height; y++) {
            memcpy(new_atlas + y * *atlas_width, *atlas + y * *atlas_width, *atlas_width * sizeof(uint32_t));
        }

        avmf_free((uint64_t)*atlas);

        *atlas = new_atlas;
        *atlas_height = new_height;
        *atlas_size = new_size;
    }

    if (pitch == 0) return AOS_FALSE;
    if ((uint64_t)pitch * height > bitmap_size) return AOS_FALSE;

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
            return AOS_FALSE;
    }

    if (pitch < minimum_pitch) return AOS_FALSE;

    uint64_t dst_x = *atlas_x;
    uint64_t dst_y = *atlas_y;

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

                default: return AOS_FALSE;
            }

            uint64_t dst_index = ((dst_y + y) * *atlas_width) + (dst_x + x);
            (*atlas)[dst_index] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | ((uint32_t)a);
        }
    }

    *atlas_x = dst_x;
    *atlas_y = dst_y;

    *atlas_x += width + PADDING;
    if (height > *atlas_row_height) *atlas_row_height = height;

    return AOS_TRUE;
}

aos_bool aosbf_to_pyrion_font(uint8_t* data, uint64_t size, struct pyrion_font* out) {
	return AOS_FALSE; // NOT TO USE
	
	if (!data || !out) return AOS_FALSE;
	if (size < sizeof(struct aos_bitmap_font_hdr)) return AOS_FALSE;

	memset(out, 0, sizeof(*out));
	out->valid = AOS_FALSE;

	struct aos_bitmap_font_hdr* hdr = (struct aos_bitmap_font_hdr*)data;
	if (memcmp(hdr->magic, AOS_BITMAP_FONT_MAGIC, AOS_BITMAP_FONT_MAGIC_LEN) != 0) return AOS_FALSE;
	if (hdr->version != AOS_BITMAP_FONT_VERSION || hdr->revision != AOS_BITMAP_FONT_REVISION) return AOS_FALSE;

	if (hdr->string_table >= size || hdr->atlas >= size) return AOS_FALSE;
	if (hdr->total_w < 1 || hdr->total_h < 1) return AOS_FALSE;

	// char* str_tab = (char*)(data + hdr->string_table);

	// Basic Info
	out->font_size = hdr->font_size;
	out->line_gap = hdr->line_gap;
	out->line_height = hdr->line_height;

	// Glyphs (Validation)
	out->glyph_count = hdr->glyph_count;

	for (uint64_t i = 0; i < hdr->glyph_count; i++) {
		uint64_t off = hdr->glyphs + (i * sizeof(struct aos_bitmap_font_glyph));
		if (off > size) return AOS_FALSE;
		
		struct aos_bitmap_font_glyph* g = (struct aos_bitmap_font_glyph*)(data + off);

		if (!g->valid) {
			out->glyph_count--;
			continue;
		}
	}

	// Glyphs (Actual Allocation and setup)
	struct pyrion_glyph* glyphs = (struct pyrion_glyph*)avmf_alloc(out->glyph_count * sizeof(struct pyrion_glyph), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
	if (!glyphs) return AOS_FALSE;
	memset(glyphs, 0, out->glyph_count * sizeof(struct pyrion_glyph));

	uint64_t atlas_size = 0;
	uint64_t atlas_w = 0;
	uint64_t atlas_h = 0;
	uint64_t atlas_row_height = 0;

	uint32_t* atlas = NULL;

	uint64_t gidx = 0;
	for (uint64_t i = 0; i < hdr->glyph_count; i++) {
		uint64_t off = hdr->glyphs + (i * sizeof(struct aos_bitmap_font_glyph));

		struct aos_bitmap_font_glyph* g = (struct aos_bitmap_font_glyph*)(data + off);
		if (!g->valid) continue;

		struct pyrion_glyph* og = &glyphs[gidx++];
		
		uint64_t atlas_x = 0;
		uint64_t atlas_y = 0;

		// Convert AOSBF Atlas to RGBA8 Pyrion ATLAS
		if (!aosbf_convert_glyph_to_rgba8(&atlas, &atlas_size, &atlas_w, &atlas_h, &atlas_row_height, &atlas_x, &atlas_y, (const uint8_t*)(data + hdr->atlas + (g->atlas_x * g->atlas_y)), g->pitch * g->width * g->height, g->width, g->height, g->pitch, g->format)) {
			if (atlas) avmf_free((uint64_t)atlas);
			atlas = NULL;

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
	}

	return AOS_TRUE;
}