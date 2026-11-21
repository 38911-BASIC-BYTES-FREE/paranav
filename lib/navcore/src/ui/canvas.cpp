#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ui {

uint32_t utf8Next(const char*& s) {
    const unsigned char* p = (const unsigned char*)s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) {
        c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += 3;
    } else if (c >= 0xE0 && p[0] && p[1]) {
        c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    } else if (c >= 0xC0 && p[0]) {
        c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
        p += 1;
    }
    s = (const char*)p;
    return c;
}

std::vector<DebugBox>* Canvas::debugSink = nullptr;
static bool gNoRecord = false;

void Canvas::clear(uint8_t c) { std::memset(fb_, (c & 0x0F) | (c << 4), W * H / 2); }

uint8_t Canvas::get(int x, int y) const {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return WHITE;
    if (flip_) { x = W - 1 - x; y = H - 1 - y; }
    uint8_t b = fb_[y * (W / 2) + (x >> 1)];
    return (x & 1) ? (b >> 4) : (b & 0x0F);
}

void Canvas::hline(int x, int y, int w, uint8_t c) {
    if (debugSink && !gNoRecord && w >= 40) debugSink->push_back({x, y, x + w - 1, y, ""});
    for (int i = 0; i < w; i++) px(x + i, y, c);
}
void Canvas::vline(int x, int y, int h, uint8_t c) {
    if (debugSink && !gNoRecord && h >= 40) debugSink->push_back({x, y, x, y + h - 1, ""});
    for (int i = 0; i < h; i++) px(x, y + i, c);
}

static inline bool patternOn(Pattern p, int x, int y) {
    switch (p) {
        case SOLID: return true;
        case HATCH: return ((x + y) & 7) < 2;
        case HATCH_SPARSE: return ((x + y) % 14) < 2;
        case DOTS: return ((x & 3) == 0) && ((y & 3) == 0);
        case MAP_DENSE: return (x + y) % 10 == 0 || (x - y + 100000) % 10 == 0;
        case MAP_MEDIUM: return (x + y) % 10 == 0;
        case MAP_LIGHT: return (x - y + 100000) % 22 == 0;
    }
    return true;
}

void Canvas::fillRect(int x, int y, int w, int h, uint8_t c, Pattern p) {
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            if (patternOn(p, xx, yy)) px(xx, yy, c);
}

void Canvas::rect(int x, int y, int w, int h, uint8_t c, int t) {
    for (int i = 0; i < t; i++) {
        hline(x, y + i, w, c);
        hline(x, y + h - 1 - i, w, c);
        vline(x + i, y, h, c);
        vline(x + w - 1 - i, y, h, c);
    }
}

// Horizontal inset of row dy (0 = top row) of a rounded rectangle of height h and radius r.
static int roundInset(int dy, int h, int r) {
    int d = std::min(dy, h - 1 - dy);  // distance from the nearer horizontal edge
    if (d >= r) return 0;
    float yy = r - d - 0.5f;
    return (int)std::lround(r - std::sqrt(std::max(0.f, (float)r * r - yy * yy)));
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, uint8_t c, Pattern p) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    for (int dy = 0; dy < h; dy++) {
        int in = roundInset(dy, h, r);
        for (int xx = x + in; xx < x + w - in; xx++)
            if (patternOn(p, xx, y + dy)) px(xx, y + dy, c);
    }
}

void Canvas::invertRoundRect(int x, int y, int w, int h, int r) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    for (int dy = 0; dy < h; dy++) {
        int in = roundInset(dy, h, r);
        for (int xx = x + in; xx < x + w - in; xx++) px(xx, y + dy, 15 - get(xx, y + dy));
    }
}

void Canvas::roundRect(int x, int y, int w, int h, int r, uint8_t c, int t) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    // straight edges
    for (int i = 0; i < t; i++) {
        hline(x + r, y + i, w - 2 * r, c);
        hline(x + r, y + h - 1 - i, w - 2 * r, c);
        vline(x + i, y + r, h - 2 * r, c);
        vline(x + w - 1 - i, y + r, h - 2 * r, c);
    }
    // corners: pixels whose centre lies in the ring r-t .. r around the corner centre
    float ro = (float)r, ri = (float)std::max(0, r - t);
    for (int dy = 0; dy < r; dy++)
        for (int dx = 0; dx < r; dx++) {
            float fx = r - dx - 0.5f, fy = r - dy - 0.5f, d = std::sqrt(fx * fx + fy * fy);
            if (d > ro || d < ri) continue;
            px(x + dx, y + dy, c);
            px(x + w - 1 - dx, y + dy, c);
            px(x + dx, y + h - 1 - dy, c);
            px(x + w - 1 - dx, y + h - 1 - dy, c);
        }
}

void Canvas::invertRect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) px(xx, yy, 15 - get(xx, yy));
}

// Liang-Barsky clip of segment to rectangle [xmin,xmax]x[ymin,ymax]; false if fully outside.
static bool clipLine(float& x0, float& y0, float& x1, float& y1, float xmin, float ymin, float xmax, float ymax) {
    float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
    float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
        } else {
            float r = q[i] / p[i];
            if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
            else { if (r < t0) return false; if (r < t1) t1 = r; }
        }
    }
    float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy, nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
}

void Canvas::line(int ix0, int iy0, int ix1, int iy1, uint8_t c, int t, int dash) {
    float fx0 = (float)ix0, fy0 = (float)iy0, fx1 = (float)ix1, fy1 = (float)iy1;
    if (!clipLine(fx0, fy0, fx1, fy1, -8.f - t, -8.f - t, W + 8.f + t, H + 8.f + t)) return;
    int x0 = (int)std::lround(fx0), y0 = (int)std::lround(fy0), x1 = (int)std::lround(fx1), y1 = (int)std::lround(fy1);
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    bool steep = -dy > dx;
    int off0 = -(t - 1) / 2;
    int n = 0;
    while (true) {
        bool on = dash <= 0 || ((n / dash) & 1) == 0;
        if (on) {
            for (int k = 0; k < t; k++) {
                if (steep) px(x0 + off0 + k, y0, c);
                else px(x0, y0 + off0 + k, c);
            }
        }
        n++;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        if (n > 4000) break;  // safety for huge off-screen lines
    }
}

void Canvas::circle(int cx, int cy, int r, uint8_t c, int t, int dash) {
    for (int k = 0; k < t; k++) {
        int rr = r - k;
        if (rr <= 0) break;
        int steps = std::max(16, (int)(2 * 3.14159f * rr));
        for (int i = 0; i < steps; i++) {
            if (dash > 0 && ((i / dash) & 1)) continue;
            float a = 2 * 3.14159265f * i / steps;
            px(cx + (int)std::lround(rr * std::cos(a)), cy + (int)std::lround(rr * std::sin(a)), c);
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int r, uint8_t c) {
    gNoRecord = true;
    for (int y = -r; y <= r; y++) {
        int w = (int)std::sqrt((float)(r * r - y * y));
        hline(cx - w, cy + y, 2 * w + 1, c);
    }
    gNoRecord = false;
}

void Canvas::fillPolygon(const Pt* p, int n, uint8_t c, Pattern pat) {
    if (n < 3) return;
    int ymin = H, ymax = -1;
    for (int i = 0; i < n; i++) {
        ymin = std::min(ymin, p[i].y);
        ymax = std::max(ymax, p[i].y);
    }
    ymin = std::max(ymin, 0);
    ymax = std::min(ymax, H - 1);
    std::vector<int> xs;
    xs.reserve(16);
    for (int y = ymin; y <= ymax; y++) {
        xs.clear();
        float yc = y + 0.5f;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float yi = (float)p[i].y, yj = (float)p[j].y;
            if ((yi > yc) != (yj > yc)) {
                float x = p[i].x + (yc - yi) * (p[j].x - p[i].x) / (yj - yi);
                xs.push_back((int)std::lround(x));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int a = std::max(0, xs[k]), b = std::min(W - 1, xs[k + 1]);
            for (int x = a; x <= b; x++)
                if (patternOn(pat, x, y)) px(x, y, c);
        }
    }
}

void Canvas::polyline(const Pt* p, int n, uint8_t c, int t, bool closed, int dash) {
    for (int i = 0; i + 1 < n; i++) line(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, c, t, dash);
    if (closed && n > 2) line(p[n - 1].x, p[n - 1].y, p[0].x, p[0].y, c, t, dash);
}

void Canvas::arrow(int cx, int cy, int len, float ang, uint8_t c, bool hollow) {
    // arrow shape in local coords (y up = forward)
    const float shape[][2] = {{0, 0.5f}, {0.36f, 0.02f}, {0.13f, 0.08f}, {0.13f, -0.5f},
                              {-0.13f, -0.5f}, {-0.13f, 0.08f}, {-0.36f, 0.02f}};
    float a = ang * 3.14159265f / 180.f, s = std::sin(a), co = std::cos(a);
    Pt pts[7];
    for (int i = 0; i < 7; i++) {
        float x = shape[i][0] * len, y = shape[i][1] * len;
        pts[i].x = cx + (int)std::lround(x * co + y * s);
        pts[i].y = cy - (int)std::lround(-x * s + y * co);
    }
    if (hollow) polyline(pts, 7, c, 3, true);
    else fillPolygon(pts, 7, c);
}

const Glyph* Canvas::findGlyph(const Font& f, uint32_t cp) {
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f.glyphs[m].cp == cp) return &f.glyphs[m];
        if (f.glyphs[m].cp < cp) lo = m + 1;
        else hi = m - 1;
    }
    return nullptr;
}

void Canvas::drawGlyph(int x, int y, const Font& f, const Glyph* g, uint8_t c) {
    int bpr = (g->w + 7) / 8;
    const uint8_t* b = f.bmp + g->off;
    for (int r = 0; r < g->h; r++)
        for (int k = 0; k < g->w; k++)
            if (b[r * bpr + (k >> 3)] & (0x80 >> (k & 7))) px(x + g->xo + k, y + g->yo + r, c);
}

int Canvas::textWidth(const char* s, const Font& f) {
    int w = 0;
    while (*s) {
        uint32_t cp = utf8Next(s);
        const Glyph* g = findGlyph(f, cp);
        if (!g) g = findGlyph(f, '?');
        if (g) w += g->adv;
    }
    return w;
}

int Canvas::text(int x, int y, const char* s, const Font& f, uint8_t c, Align a) {
    int w = textWidth(s, f);
    if (a == CENTER) x -= w / 2;
    else if (a == RIGHT) x -= w;
    if (debugSink) {
        int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30), pen = x;
        const char* p = s;
        while (*p) {
            const Glyph* g = findGlyph(f, utf8Next(p));
            if (!g) g = findGlyph(f, '?');
            if (!g) continue;
            if (g->w && g->h) {
                bx0 = std::min(bx0, pen + g->xo);
                bx1 = std::max(bx1, pen + g->xo + g->w - 1);
                by0 = std::min(by0, y + g->yo);
                by1 = std::max(by1, y + g->yo + g->h - 1);
            }
            pen += g->adv;
        }
        if (bx1 >= bx0) debugSink->push_back({bx0, by0, bx1, by1, s});
    }
    while (*s) {
        uint32_t cp = utf8Next(s);
        const Glyph* g = findGlyph(f, cp);
        if (!g) g = findGlyph(f, '?');
        if (!g) continue;
        drawGlyph(x, y, f, g, c);
        x += g->adv;
    }
    return w;
}

int Canvas::textInBox(int x, int yTop, int h, const char* s, const Font& f, uint8_t c, Align a) {
    // approximate cap-height centring
    int capH = f.ascent * 72 / 100;
    int base = yTop + (h + capH) / 2;
    return text(x, base, s, f, c, a);
}

}  // namespace ui
