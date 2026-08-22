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

#define AOSBF_ATLAS_INITIAL_SIZE 4096
#define AOSBF_ATLAS_PADDING 1

static uint8_t* atlas = NULL; // Not exactly an atlas
static uint64_t atlas_off = 0;
static uint64_t atlas_cap = 0;

static int32_t ft26_6_to_i32(FT_Pos v) {
    if (v >= 0) return (int32_t)((v + 32) >> 6);
    else return -(int32_t)((-v + 32) >> 6);
}

static enum aos_bitmap_font_format get_bitmap_aosbf_cformat(FT_Pixel_Mode pixel_mode) {
	switch (pixel_mode) {
		case FT_PIXEL_MODE_MONO: return AOSBF_FORMAT_MONO;
		case FT_PIXEL_MODE_GRAY: return AOSBF_FORMAT_A8;
		case FT_PIXEL_MODE_GRAY2: return AOSBF_FORMAT_GRAY2;
		case FT_PIXEL_MODE_GRAY4: return AOSBF_FORMAT_GRAY4;
		
		default: return AOSBF_FORMAT_INVALID;
	}
}

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

static bool atlas_resize(uint64_t new_cap) {
    if (new_cap == 0 || new_cap < atlas_cap) return false;
	if (new_cap == atlas_cap) return true;

    uint8_t* new_atlas = realloc(atlas, new_cap);
    if (!new_atlas) return false;

	memset(new_atlas + atlas_cap, 0, new_cap - atlas_cap);

    atlas = new_atlas;
    atlas_cap = new_cap;

    return true;
}

static bool atlas_init(void) {
    atlas = NULL;
    atlas_off = 0;
	atlas_cap = 0;

    return atlas_resize(AOSBF_ATLAS_INITIAL_SIZE);
}

static struct atlas_position push_bitmap(const FT_Bitmap* bitmap) {
    struct atlas_position invalid = {
		.valid = false,
		.x = UINT32_MAX,
		.y = UINT32_MAX
	};

    if (!bitmap) return invalid;
    if (bitmap->width == 0 || bitmap->rows == 0) return invalid;

	uint64_t size = (uint64_t)bitmap->pitch * bitmap->rows;
	uint64_t required = atlas_off + size;

	if (required > atlas_cap) {
		if (!atlas_resize(required)) return invalid;
	}

    uint64_t offset = atlas_off;
    memcpy(atlas + offset, bitmap->buffer, size);
    atlas_off += size;

    struct atlas_position position = { // Take advantage of aosbf
        .x = offset,
        .y = 0,
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
    } else {
		fprintf(stderr, "Error: Invalid/missing \"PIXEL_SIZE\"\n");
    	return false;
	}

	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size) != 0) {
		fprintf(stderr, "Error: Failed to set FreeType pixel size\n");
		return false;
	}

	if (!face->size) {
		fprintf(stderr, "Error: FreeType size object unavailable\n");
		return false;
	}
	FT_Size_Metrics* m = &face->size->metrics;

	FT_Pos ascent = m->ascender;
	FT_Pos descent = -m->descender;
	FT_Pos height = m->height;

	FT_Pos line_gap = height - ascent - descent;
	if (line_gap < 0) line_gap = 0;
	
	hdr->ascent = (int16_t)ft26_6_to_i32(ascent);
	hdr->descent = (int16_t)ft26_6_to_i32(descent);
	hdr->line_height = (int16_t)ft26_6_to_i32(height);
	hdr->line_gap = (int16_t)ft26_6_to_i32(line_gap);

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

	uint32_t fallback_glyph = 0;

    while (glyph_index != 0) {
        if (glyph_no >= face->num_glyphs) break;

        struct aos_bitmap_font_glyph* glyph = &glyphs[glyph_no];
        memset(glyph, 0, sizeof(*glyph));
		glyph->valid = AOS_BITMAP_FONT_FALSE;

        glyph->codepoint = (uint32_t)charcode;
		if (charcode == '?' && fallback_glyph != 0xFFFD) {
			glyph->is_fallback = AOS_BITMAP_FONT_TRUE; // Incase U+FFFD doesn't exist
			fallback_glyph = '?';
		}
		if (charcode == 0xFFFD) {
			glyph->is_fallback = AOS_BITMAP_FONT_TRUE;
			fallback_glyph = 0xFFFD;
		}

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

		enum aos_bitmap_font_format font_format = get_bitmap_aosbf_cformat(slot->bitmap.pixel_mode);
		if (font_format == AOSBF_FORMAT_INVALID) {
			fprintf(stderr, "Error: Unsupported/Invalid font format for glyph U+%04lX\n", charcode);
            return false;
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
		glyph->pitch = (uint16_t)slot->bitmap.pitch;

        glyph->bearing_x = (int16_t)slot->bitmap_left;
		glyph->bearing_y = (int16_t)slot->bitmap_top;

        glyph->advance_x = (int16_t)(slot->advance.x >> 6);
        glyph->advance_y = (int16_t)(slot->advance.y >> 6);

		glyph->format = font_format;

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
	hdr->string_table = offset;
    offset += str_table_off;

    hdr->glyphs = sizeof(*hdr) + str_table_off;
    uint64_t glyph_size = (uint64_t)hdr->glyph_count * sizeof(struct aos_bitmap_font_glyph);

    uint64_t atlas_offset = hdr->glyphs + glyph_size;
    hdr->total_w = atlas_off;
    hdr->total_h = 1;
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

    uint64_t atlas_size = (uint64_t)atlas_off;

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
    printf("\tAtlas:    %llu bytes\n", (unsigned long long)atlas_size);

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
		atlas_off = 0;
		atlas_cap = 0;

		return out;
	}

	fail: {
		out = false;
		goto cleanup_and_exit;
	}
}
