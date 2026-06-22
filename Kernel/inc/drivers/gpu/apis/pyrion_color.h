#pragma once

enum pyrion_color_format {
    PYRION_COLORF_RGBA,
    PYRION_COLORF_BGRA,
    PYRION_COLORF_ABGR,
    PYRION_COLORF_ARGB,
    PYRION_COLORF_RGB, // A is always 0xFF
    PYRION_COLORF_BGR, // A is always 0xFF
};