#pragma once

#define AOS_BITMAP_FONT_MAGIC "AOSBF"
#define AOS_BITMAP_FONT_MAGIC_LEN 6

#define AOS_BITMAP_FONT_VERSION 0x000100
#define AOS_BITMAP_FONT_REVISION 0

#define AOS_BITMAP_FONT_TRUE 1
#define AOS_BITMAP_FONT_FALSE 0

struct aos_bitmap_font_hdr {
	char magic[AOS_BITMAP_FONT_MAGIC_LEN]; // Signature
	uint32_t version; // Version
	uint16_t revision; // Revision

	uint64_t font_name; // An Offset into the string table specifying the name of the font
	uint64_t font_family_name; // An Offset into the string table specifying the name of the font family
	uint64_t author_name; // An Offset into the string table specifying the name of the author of the font

	uint16_t font_size; // The font size the font was designed for

	int16_t line_gap; // The gap between lines
	int16_t line_height; // The height of each line
	
	uint64_t comments; // An Offset into the string table specifying comments
	uint16_t comment_count; // The number of comments

	uint32_t total_w; // Total width of the entire font atlas
	uint32_t total_h; // Total height of the entire font atlas
	uint64_t atlas; // An Offset from the start of the file pointing to AOS Bitmap Font Atlas

	uint64_t glyphs; // An Offset from the start of the file pointing to AOS Bitmap Font Glyphs
	uint32_t glyph_count; // Number of glyphs present
};

struct aos_bitmap_font_glyph {
	uint32_t codepoint; // The Corrosponding Unicode Character of the Glyph

    uint32_t atlas_x; // Glyph X Coordinate in Atlas
    uint32_t atlas_y; // Glyph Y Coordinate in Atlas

    uint16_t width; // Glyph Width
    uint16_t height; // Glyph Height

    int16_t bearing_x; // Glyph X Bearing
    int16_t bearing_y; // Glyph Y Bearing

    int16_t advance_x; // Glyph - X Advancement value after rendering
    int16_t advance_y; // Glyph - Y Advancement value after rendering

	uint8_t valid;
};