// 4-bit grayscale canvas drawing directly into an epdiy-compatible framebuffer
// (2 px per byte, even x in low nibble). 0 = black, 15 = white.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "fonts.h"

namespace ui {

constexpr int W = 960;
constexpr int H = 540;
constexpr uint8_t BLACK = 0, DARK = 5, GRAY = 9, LIGHT = 12, WHITE = 15;

enum Align { LEFT, CENTER, RIGHT };
enum Pattern {
    SOLID, HATCH, HATCH_SPARSE, DOTS,
    MAP_DENSE,   // map: prohibited / restricted / danger - 1 px cross-hatch every 10 px
    MAP_MEDIUM,  // map: CTR, TRA/TSA - 1 px diagonal every 10 px
    MAP_LIGHT    // map: other controlled airspace (TMA, ...) - 1 px diagonal every 22 px
};

struct Pt {
    int x, y;
};

// Layout debugging (desktop tools only): when Canvas::debugSink is set, every text call
// records its tight bounding box and every straight separator line is recorded too.
struct DebugBox {
    int x0, y0, x1, y1;  // inclusive
    std::string text;    // empty for separator lines
};

class Canvas {
public:
    Canvas(uint8_t* fb) : fb_(fb) {}
    void setFlip(bool f) { flip_ = f; }
    static std::vector<DebugBox>* debugSink;
    uint8_t* fb() { return fb_; }

    void clear(uint8_t c = WHITE);
    inline void px(int x, int y, uint8_t c) {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
        if (flip_) { x = W - 1 - x; y = H - 1 - y; }
        uint8_t* p = &fb_[y * (W / 2) + (x >> 1)];
        if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
        else *p = (uint8_t)((*p & 0xF0) | (c & 0x0F));
    }
    uint8_t get(int x, int y) const;
    void hline(int x, int y, int w, uint8_t c);
    void vline(int x, int y, int h, uint8_t c);
    void fillRect(int x, int y, int w, int h, uint8_t c, Pattern p = SOLID);
    void rect(int x, int y, int w, int h, uint8_t c, int t = 1);
    void invertRect(int x, int y, int w, int h);
    // Rounded rectangles (radius r, clamped to half the shorter side). The straight edges of
    // roundRect count as separator lines for the layout checker.
    void roundRect(int x, int y, int w, int h, int r, uint8_t c, int t = 2);
    void fillRoundRect(int x, int y, int w, int h, int r, uint8_t c, Pattern p = SOLID);
    void invertRoundRect(int x, int y, int w, int h, int r);
    void line(int x0, int y0, int x1, int y1, uint8_t c, int t = 1, int dash = 0);
    void circle(int cx, int cy, int r, uint8_t c, int t = 1, int dash = 0);
    void fillCircle(int cx, int cy, int r, uint8_t c);
    void fillPolygon(const Pt* p, int n, uint8_t c, Pattern pat = SOLID);
    void polyline(const Pt* p, int n, uint8_t c, int t = 1, bool closed = false, int dash = 0);
    // Filled arrow pointing to angle (deg, 0 = up/screen-north, clockwise), length L.
    void arrow(int cx, int cy, int len, float angleDeg, uint8_t c, bool hollow = false);

    // Text: (x, y) is the baseline anchor. Returns advance width.
    int text(int x, int y, const char* s, const Font& f, uint8_t c = BLACK, Align a = LEFT);
    int text(int x, int y, const std::string& s, const Font& f, uint8_t c = BLACK, Align a = LEFT) {
        return text(x, y, s.c_str(), f, c, a);
    }
    static int textWidth(const char* s, const Font& f);
    static bool hasGlyph(const Font& f, uint32_t cp) { return findGlyph(f, cp) != nullptr; }
    // Text vertically centred in a box (yTop..yTop+h)
    int textInBox(int x, int yTop, int h, const char* s, const Font& f, uint8_t c, Align a);

private:
    static const Glyph* findGlyph(const Font& f, uint32_t cp);
    void drawGlyph(int x, int y, const Font& f, const Glyph* g, uint8_t c);
    uint8_t* fb_;
    bool flip_ = false;
};

// UTF-8 decoder: returns codepoint and advances s.
uint32_t utf8Next(const char*& s);

}  // namespace ui
