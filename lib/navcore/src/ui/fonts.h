#pragma once
#include <cstdint>

namespace ui {

struct Glyph {
    uint16_t cp;
    uint8_t w, h;
    int8_t xo, yo;  // offset of bitmap top-left from pen position at baseline
    uint8_t adv;
    uint32_t off;   // into bitmap; rows are byte aligned, MSB first
};

struct Font {
    const uint8_t* bmp;
    const Glyph* glyphs;
    uint16_t count;
    uint8_t lineH;
    uint8_t ascent;
};

extern const Font font_s;    // 22 px bold condensed - small labels
extern const Font font_sb;   // 25 px bold condensed - labels, list text
extern const Font font_m;    // 32 px bold condensed - secondary values
extern const Font font_l;    // 50 px bold condensed - primary values
extern const Font font_xl;   // 80 px bold condensed - key values
extern const Font font_xxl;  // 130 px bold condensed - digits only

}  // namespace ui
