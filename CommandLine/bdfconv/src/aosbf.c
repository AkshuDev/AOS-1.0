#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BDF_H

#include <aosbf.h>
#include <aos_bitmap_font.h>

struct atlas_position {
    uint32_t x;
    uint32_t y;

	bool valid;
};

static char* str_table = NULL;

static uint64_t str_table_off = 0;
static uint64_t str_table_capacity = 0;

#define AOSBF_ATLAS_INITIAL_W 256
#define AOSBF_ATLAS_INITIAL_H 256
#define AOSBF_ATLAS_PADDING 1

static uint8_t* atlas = NULL;

static uint32_t atlas_w = 0;
static uint32_t atlas_h = 0;

static uint32_t atlas_x = 0;
static uint32_t atlas_y = 0;
static uint32_t atlas_row_h = 0;

static uint64_t push_string(const char* str) {
    if (!str) return UINT64_MAX;

    size_t len = strlen(str) + 1;

	uint64_t required = str_table_off + len;
    if (required > str_table_capacity) {
        uint64_t new_capacity = str_table_capacity;
	
		if (new_capacity == 0) new_capacity = 128;
       	while (new_capacity < required) {
            if (new_capacity > UINT64_MAX / 2)
                new_capacity = required;
            else
                new_capacity += 128;
        }

        char* new_str_table = realloc(str_table, new_capacity * sizeof(char));
        if (!new_str_table) return UINT64_MAX;

        str_table = new_str_table;
        str_table_capacity = new_capacity;
    }

    memcpy(str_table + str_table_off, str, len);
	uint64_t ret = str_table_off;

	str_table_off += len;

    return ret;
}

static bool atlas_resize(uint32_t new_w, uint32_t new_h) {
    if (new_w == 0 || new_h == 0) return false;
    if (new_w < atlas_w || new_h < atlas_h) return false;

    uint64_t pixel_count = (uint64_t)new_w * new_h;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t)) return false;
    size_t new_size = (size_t)pixel_count * sizeof(uint32_t);

    uint8_t* new_atlas = calloc(sizeof(uint8_t), new_size);
    if (!new_atlas) return false;

	if (atlas) {
        for (uint32_t y = 0; y < atlas_h; y++) {
            memcpy(new_atlas + ((size_t)y * new_w * sizeof(uint32_t)), atlas + ((size_t)y * atlas_w * sizeof(uint32_t)), (size_t)atlas_w * sizeof(uint32_t));
        }

        free(atlas);
    }

    atlas = new_atlas;
    atlas_w = new_w;
    atlas_h = new_h;

    return true;
}

static bool atlas_init(void) {
    atlas = NULL;

    atlas_w = 0;
    atlas_h = 0;

    atlas_x = 0;
    atlas_y = 0;
    atlas_row_h = 0;

    return atlas_resize(AOSBF_ATLAS_INITIAL_W, AOSBF_ATLAS_INITIAL_H);
}

static bool atlas_ensure_space(uint32_t width, uint32_t height, uint32_t* out_x, uint32_t* out_y) {
    if (!out_x || !out_y) return false;
    if (width == 0 || height == 0) return false;

    uint64_t padded_w = (uint64_t)width + AOSBF_ATLAS_PADDING;
    uint64_t padded_h = (uint64_t)height + AOSBF_ATLAS_PADDING;

    if (padded_w > UINT32_MAX || padded_h > UINT32_MAX) return false;

    if ((uint64_t)atlas_x + padded_w > atlas_w) {
        atlas_x = 0;
        if (atlas_row_h > UINT32_MAX - AOSBF_ATLAS_PADDING) return false;
        
		atlas_y += atlas_row_h + AOSBF_ATLAS_PADDING;
        atlas_row_h = 0;
    }

    uint64_t required_h = (uint64_t)atlas_y + padded_h;

    if (required_h > atlas_h) {
        uint32_t new_h = atlas_h;
        if (new_h == 0) new_h = AOSBF_ATLAS_INITIAL_H;

        while ((uint64_t)new_h < required_h) {
            if (new_h > UINT32_MAX / 2)
                new_h = (uint32_t)required_h;
            else
                new_h += 256;
        }

        if (!atlas_resize(atlas_w, new_h)) return false;
    }

    if ((uint64_t)width > atlas_w) return false;

    *out_x = atlas_x;
    *out_y = atlas_y;

    atlas_x += width + AOSBF_ATLAS_PADDING;
    if (height > atlas_row_h) atlas_row_h = height;

    return true;
}

static struct atlas_position push_bitmap(const FT_Bitmap* bitmap) {
    struct atlas_position invalid = {
		.valid = false,
		.x = UINT32_MAX,
		.y = UINT32_MAX
	};

    if (!bitmap) return invalid;
    if (bitmap->width == 0 || bitmap->rows == 0) return invalid;

    uint32_t x = 0;
    uint32_t y = 0;

    if (!atlas_ensure_space(bitmap->width, bitmap->rows, &x, &y)) return invalid;

    for (uint32_t src_y = 0; src_y < bitmap->rows; src_y++) {
        for (uint32_t src_x = 0; src_x < bitmap->width; src_x++) {
            uint8_t alpha = 0;

            if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
                alpha = bitmap->buffer[src_y * bitmap->pitch + src_x];
            } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                uint8_t byte = bitmap->buffer[src_y * bitmap->pitch + (src_x >> 3)];
                uint8_t bit = 7 - (src_x & 7);
                alpha = (byte & (1 << bit)) ? 255 : 0;
            } else {
                fprintf(stderr, "Error: Unsupported FreeType bitmap pixel mode: %u\n", bitmap->pixel_mode);
                return invalid;
            }

            size_t dst = (((size_t)y + src_y) * atlas_w + ((size_t)x + src_x)) * 4;

            atlas[dst + 0] = 255;
            atlas[dst + 1] = 255;
            atlas[dst + 2] = 255;
            atlas[dst + 3] = alpha;
        }
    }

    struct atlas_position position = {
        .x = x,
        .y = y,
		.valid = true
    };

    return position;
}

static bool get_bdf_integer_property(FT_Face face, const char* name, FT_Int32* value) {
    if (!face || !name || !value) return false;

    BDF_PropertyRec prop;
    FT_Error error = FT_Get_BDF_Property(face, name, &prop);

    if (error) return false;
    if (prop.type != BDF_PROPERTY_TYPE_INTEGER) return false;

    *value = prop.u.integer;

    return true;
}

static bool build_font_header(FT_Face face, struct aos_bitmap_font_hdr* hdr) {
    if (!face || !hdr) {
		fprintf(stderr, "Error: NULL Face or Header Supplied!\n");
		return false;
	}
    memset(hdr, 0, sizeof(*hdr));

    memcpy(hdr->magic, AOS_BITMAP_FONT_MAGIC, AOS_BITMAP_FONT_MAGIC_LEN);
    hdr->version = AOS_BITMAP_FONT_VERSION;
    hdr->revision = AOS_BITMAP_FONT_REVISION;

	if (face->family_name) {
        uint64_t off = push_string(face->family_name);
        if (off == UINT64_MAX) {
            fprintf(stderr, "Error: Failed to store font name\n");
            return false;
        }

        hdr->font_family_name = off;
		hdr->font_name = off;
    }

    FT_Int32 pixel_size = 0;
    if (get_bdf_integer_property(face, "PIXEL_SIZE", &pixel_size)) {
        if (pixel_size >= 0 && pixel_size <= UINT16_MAX) {
            hdr->font_size = (uint16_t)pixel_size;
        }
    }

    if (face->height >= INT16_MIN && face->height <= INT16_MAX) {
        hdr->line_height = (int16_t)face->height;
    }

    if (face->num_glyphs >= 0 && face->num_glyphs <= UINT32_MAX) {
        hdr->glyph_count = (uint32_t)face->num_glyphs;
    }

    return true;
}

static bool build_glyphs(FT_Face face, struct aos_bitmap_font_glyph* glyphs, uint32_t* glyph_count) {
    if (!face || !glyphs) return false;

    FT_ULong charcode;
    FT_UInt glyph_index;

    charcode = FT_Get_First_Char(face, &glyph_index);
    uint32_t glyph_no = 0;

    while (glyph_index != 0) {
        if (glyph_no >= face->num_glyphs) break;

        struct aos_bitmap_font_glyph* glyph = &glyphs[glyph_no];
        memset(glyph, 0, sizeof(*glyph));
		glyph->valid = AOS_BITMAP_FONT_FALSE;

        glyph->codepoint = (uint32_t)charcode;

        FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);

        if (error) {
            fprintf(stderr, "Warning: Failed to load U+%04lX\n", charcode);
            charcode = FT_Get_Next_Char(face, charcode, &glyph_index);

            glyph_no++;
            continue;
        }

        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        if (error) {
            fprintf(stderr, "Warning: Failed to render U+%04lX\n", charcode);
            charcode = FT_Get_Next_Char(face, charcode, &glyph_index);

            glyph_no++;
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
		if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
			glyph->atlas_x = 0;
			glyph->atlas_y = 0;

			glyph->width = 0;
			glyph->height = 0;

			glyph->bearing_x = (int16_t)slot->bitmap_left;
			glyph->bearing_y = (int16_t)slot->bitmap_top;

			glyph->advance_x = (int16_t)(slot->advance.x >> 6);
			glyph->advance_y = (int16_t)(slot->advance.y >> 6);

			glyph->valid = AOS_BITMAP_FONT_TRUE;
			glyph_no++;

			charcode = FT_Get_Next_Char(face, charcode, &glyph_index);
			continue;
		}

        struct atlas_position pos = push_bitmap(&slot->bitmap);

        if (!pos.valid) {
            fprintf(stderr, "Error: Failed to push glyph U+%04lX into atlas\n", charcode);
            return false;
        }

        glyph->atlas_x = pos.x;
        glyph->atlas_y = pos.y;

        glyph->width = (uint16_t)slot->bitmap.width;
        glyph->height = (uint16_t)slot->bitmap.rows;

        glyph->bearing_x = (int16_t)slot->bitmap_left;
		glyph->bearing_y = (int16_t)slot->bitmap_top;

        glyph->advance_x = (int16_t)(slot->advance.x >> 6);
        glyph->advance_y = (int16_t)(slot->advance.y >> 6);

        glyph->valid = AOS_BITMAP_FONT_TRUE;
        glyph_no++;

        charcode = FT_Get_Next_Char(face, charcode, &glyph_index);
    }

	*glyph_count = glyph_no;
    return true;
}

static bool write_aosbf(const char* path, struct aos_bitmap_font_hdr* hdr, struct aos_bitmap_font_glyph* glyphs) {
    if (!path || !hdr || !glyphs) return false;

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open output file '%s'\n", path);
        return false;
    }

    uint64_t offset = sizeof(*hdr);
    offset += str_table_off;

    hdr->glyphs = sizeof(*hdr) + str_table_off;
    uint64_t glyph_size = (uint64_t)hdr->glyph_count * sizeof(struct aos_bitmap_font_glyph);

    uint64_t atlas_offset = hdr->glyphs + glyph_size;
    hdr->total_w = atlas_w;
    hdr->total_h = atlas_h;
	hdr->atlas = atlas_offset;

	fseek(fp, 0, SEEK_SET);

    if (fwrite(hdr, sizeof(*hdr), 1, fp) < 1) {
        fprintf(stderr, "Error: Failed to write AOSBF header\n");

        fclose(fp);
        return false;
    }

    if (str_table_off > 0) {
        if (fwrite(str_table, str_table_off, 1, fp) < 1) {
			fprintf(stderr, "Error: Failed to write String Table\n");

			fclose(fp);
			return false;
		}
    }

    if (glyph_size > 0) {
        if (fwrite(glyphs, glyph_size, 1, fp) < 1) {
			fprintf(stderr, "Error: Failed to write Glyphs\n");

			fclose(fp);
			return false;
		}
    }

    uint64_t atlas_size = (uint64_t)atlas_w * atlas_h * sizeof(uint32_t);

    if (atlas_size > 0) {
        if (fwrite(atlas, atlas_size, 1, fp) < 1) {
			fprintf(stderr, "Error: Failed to write Atlas\n");

			fclose(fp);
			return false;
		}
    }

	fflush(fp);
    fclose(fp);

    printf("AOSBF written: %s\n", path);

    printf("\tHeader :  %zu bytes\n", sizeof(*hdr));
    printf("\tStrings:  %llu bytes\n", (unsigned long long)str_table_off);
    printf("\tGlyphs:   %llu bytes\n", (unsigned long long)glyph_size);
    printf("\tAtlas:    %llu bytes (%ux%u RGBA8)\n", (unsigned long long)atlas_size, atlas_w, atlas_h);

    printf("Total:    %llu bytes\n", (unsigned long long)(sizeof(*hdr) + str_table_off + glyph_size + atlas_size ));

    return true;
}

bool build_aosbf(const char* output_path, FT_Face face) {
    if (!output_path) {
        fprintf(stderr, "Error: NULL output path\n");
        return false;
    }

    if (!face) {
        fprintf(stderr, "Error: NULL FreeType face\n");
        return false;
    }

    printf("Building AOSBF: %s\n", output_path);
	bool out = true;

    struct aos_bitmap_font_hdr hdr = {0};
	if (!build_font_header(face, &hdr)) goto fail;

	struct aos_bitmap_font_glyph* glyphs = NULL;
	glyphs = calloc(face->num_glyphs, sizeof(*glyphs));
	if (!glyphs) {
		fprintf(stderr, "Error: Failed to allocate glyph table\n");
		goto fail;
	}

	if (!atlas_init()) {
		fprintf(stderr, "Error: Failed to initialize atlas\n");
		goto fail;
	}

	if (!build_glyphs(face, glyphs, &hdr.glyph_count)) {
		fprintf(stderr, "Error: Failed to build glyph table\n");
		goto fail;
	}

	if (!write_aosbf(output_path, &hdr, glyphs)) goto fail;

	cleanup_and_exit: {
		if (str_table) free(str_table);
		str_table = NULL;

		str_table_off = 0;
		str_table_capacity = 0;

		if (glyphs) free(glyphs);
		glyphs = NULL;

		if (atlas) free(atlas);
		atlas = NULL;

		atlas_w = 0;
		atlas_h = 0;

		atlas_x = 0;
		atlas_y = 0;
		atlas_row_h = 0;

		return out;
	}

	fail: {
		out = false;
		goto cleanup_and_exit;
	}
}
