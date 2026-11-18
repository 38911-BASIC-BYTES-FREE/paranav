#include "screens.h"
#include "../i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

using nav::NavState;

// ------------------------------------------------------------------ formatting
std::string fmtInt(float v, const char* invalid) {
    if (std::isnan(v)) return invalid;
    char b[16];
    std::snprintf(b, sizeof b, "%d", (int)std::lround(v));
    return b;
}
std::string fmtSigned1(float v) {
    if (std::isnan(v)) return "––";
    char b[16];
    if (std::fabs(v) < 0.05f) v = 0;
    std::snprintf(b, sizeof b, "%+.1f", (double)v);
    return b;
}
std::string fmtDistKm(float m) {
    if (std::isnan(m)) return "––";
    char b[16];
    float km = m / 1000.f;
    if (km < 9.95f) std::snprintf(b, sizeof b, "%.1f", (double)km);
    else std::snprintf(b, sizeof b, "%d", (int)std::lround(km));
    return b;
}
std::string fmtDur(float sec, bool hm) {
    if (std::isnan(sec) || sec < 0) return "––";
    char b[16];
    int s = (int)std::lround(sec);
    if (hm) std::snprintf(b, sizeof b, "%d:%02d", s / 3600, (s / 60) % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
std::string fmtClock(uint32_t sec) {
    char b[8];
    sec %= 86400;
    std::snprintf(b, sizeof b, "%02u:%02u", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60));
    return b;
}
static std::string fmtDeg(float d) {
    char b[8];
    std::snprintf(b, sizeof b, "%03d°", ((int)std::lround(d) + 360) % 360);
    return b;
}
static std::string trunc(const std::string& s, const Font& f, int maxW) {
    if (Canvas::textWidth(s.c_str(), f) <= maxW) return s;
    std::string r = s;
    while (!r.empty() && Canvas::textWidth((r + "…").c_str(), f) > maxW) {
        // drop last UTF-8 char
        size_t i = r.size() - 1;
        while (i > 0 && ((unsigned char)r[i] & 0xC0) == 0x80) i--;
        r.erase(i);
    }
    return r + ".";
}

const char* screenName(ScreenId s) {
    const char* n[] = {TR("LOT", "FLIGHT"), TR("MAPA", "MAP"), "TOPO", TR("RUCH", "TRAFFIC"), TR("NAWIGACJA", "NAV"), TR("TERMIKA", "THERMAL"), TR("STREFY", "AIRSPACE"), TR("PALIWO", "FUEL"), "SYSTEM"};
    return (s >= 0 && s < SCR_COUNT) ? n[s] : "";
}

static uint32_t localSec(const UiContext& u) {
    int t = (int)u.nav->utcSec + u.set->tzOffsetMin * 60;
    return (uint32_t)((t % 86400 + 86400) % 86400);
}

static void label(Canvas& c, int x, int y, const char* s, Align a = LEFT) { c.text(x, y, s, font_s, BLACK, a); }

static const char* srcSuffix(nav::AltSource s) {
    switch (s) {
        case nav::AltSource::BlueFly: return "  BlueFly";
        case nav::AltSource::Gps: return "  (GPS)";
        default: return "";
    }
}

// value + unit, right aligned at xr
static void valueUnit(Canvas& c, int xr, int base, const std::string& v, const Font& f, const char* unit) {
    int uw = unit && *unit ? Canvas::textWidth(unit, font_sb) + 6 : 0;
    c.text(xr - uw, base, v, f, BLACK, RIGHT);
    if (uw) c.text(xr, base, unit, font_sb, BLACK, RIGHT);
}

// ------------------------------------------------------------------ status bar
void drawStatusBar(Canvas& c, ScreenId s, const UiContext& u, const char* title) {
    const NavState& n = *u.nav;
    c.fillRect(0, 0, W, 40, WHITE);
    c.hline(0, 39, W, BLACK);
    c.hline(0, 38, W, BLACK);
    c.text(10, 30, title ? title : screenName(s), font_sb, BLACK);
    // right side: alert, REC, GPS, battery
    int x = W - 10;
    // battery
    int bp = u.sys ? u.sys->battPct : -1;
    c.rect(x - 52, 10, 46, 22, BLACK, 2);
    c.fillRect(x - 6, 16, 4, 10, BLACK);
    if (bp >= 0) c.fillRect(x - 49, 13, std::max(0, std::min(40, bp * 40 / 100)), 16, BLACK);
    char b[24];
    std::snprintf(b, sizeof b, bp >= 0 ? "%d%%" : "?", bp);
    x -= 60;
    x -= c.text(x, 30, b, font_sb, BLACK, RIGHT) + 4;
    if (u.sys && u.sys->charging) x -= c.text(x, 30, "⚡", font_sb, BLACK, RIGHT) + 4;
    x -= 16;
    // GPS
    if (n.fix) std::snprintf(b, sizeof b, "GPS %d", n.sats);
    else std::snprintf(b, sizeof b, "GPS –");
    int gw = c.text(x, 30, b, font_sb, BLACK, RIGHT);
    if (!n.fix) c.invertRect(x - gw - 4, 4, gw + 8, 32);
    x -= gw + 20;
    if (u.sys && u.sys->recording) {
        int rw = c.text(x, 30, "REC", font_sb, BLACK, RIGHT);
        c.fillCircle(x - rw - 12, 20, 7, BLACK);
        x -= rw + 34;
    }
    if (u.sys && u.sys->phoneState >= 2) {
        // phone connected (Android app)
        int w = Canvas::textWidth("TEL", font_sb) + 10;
        c.rect(x - w, 4, w, 32, BLACK, 2);
        c.text(x - w / 2, 30, "TEL", font_sb, BLACK, CENTER);
        x -= w + 8;
    }
    if (u.traffic) {
        // FANET: number of aircraft heard in the last minute; inverted on a caution/alarm
        int cnt = 0, lvl = 0;
        for (const auto& t : *u.traffic)
            if (t.lastMs && t.ageS <= 60 && !t.ground) {
                cnt++;
                lvl = std::max(lvl, t.level);
            }
        if (cnt > 0) {
            std::snprintf(b, sizeof b, "FN %d", cnt);
            int w = Canvas::textWidth(b, font_sb) + 10;
            if (lvl >= 2) c.fillRect(x - w, 4, w, 32, BLACK);
            else c.rect(x - w, 4, w, 32, BLACK, 2);
            c.text(x - w / 2, 30, b, font_sb, lvl >= 2 ? WHITE : BLACK, CENTER);
            x -= w + 8;
        }
    }
    if (u.sys && u.sys->bfState == 3) {
        // BlueFly connected; inverted when it is the active altitude source
        bool act = n.altSource == nav::AltSource::BlueFly;
        int w = Canvas::textWidth("BF", font_sb) + 10;
        if (act) c.fillRect(x - w, 4, w, 32, BLACK);
        else c.rect(x - w, 4, w, 32, BLACK, 2);
        c.text(x - w / 2, 30, "BF", font_sb, act ? WHITE : BLACK, CENTER);
        x -= w + 8;
    }
    if (u.sys && u.sys->backlight) {
        // front light ON: inverted box with a sun icon - easy to spot that it drains the battery
        int bx = x - 34;
        c.fillRect(bx, 4, 34, 32, BLACK);
        int cx = bx + 17, cy = 20;
        c.fillCircle(cx, cy, 5, WHITE);
        for (int k = 0; k < 8; k++) {
            float a = k * 3.14159265f / 4;
            c.line(cx + (int)std::lround(std::cos(a) * 8), cy + (int)std::lround(std::sin(a) * 8),
                   cx + (int)std::lround(std::cos(a) * 12), cy + (int)std::lround(std::sin(a) * 12), WHITE, 2);
        }
        x -= 34 + 8;
    }
    if (u.alerts) {
        int a = u.alerts->topAny();
        if (a >= 0) {
            c.fillRect(x - 30, 4, 30, 32, BLACK);
            c.text(x - 15, 30, "!", font_sb, WHITE, CENTER);
            x -= 30 + 8;
        }
    }
    // centre: clock + flight time, placed after the right-hand icons are known (x = their left edge)
    int titleEnd = 10 + Canvas::textWidth(title ? title : screenName(s), font_sb);
    std::string clk = n.timeValid ? fmtClock(localSec(u)) : "--:--";
    std::string ft = n.phase == nav::Phase::Flying ? "▲ " + fmtDur((float)n.flightTimeS) : "";
    int cw = Canvas::textWidth(clk.c_str(), font_sb), fw = ft.empty() ? 0 : Canvas::textWidth(ft.c_str(), font_sb) + 12;
    int cx = std::min(305, x - 12 - cw - fw);
    bool dots = !title && cx >= 190 + SCR_COUNT * 12 + 4;  // page dots only when there is room
    if (dots) {
        for (int i = 0; i < SCR_COUNT; i++) {
            int dx = 190 + i * 12;
            if (i == s) c.fillCircle(dx, 20, 5, BLACK);
            else c.circle(dx, 20, 5, BLACK, 1);
        }
    }
    if (cx < titleEnd + 12) {  // very crowded: keep the flight time, drop the clock
        cw = -12;
        cx = std::max(titleEnd + 12, x - 12 - fw);
    } else {
        c.text(cx, 30, clk, font_sb, BLACK);
    }
    if (!ft.empty()) c.text(cx + cw + 12, 30, ft, font_sb, BLACK);
}

// ------------------------------------------------------------------ FLIGHT
static void varioBar(Canvas& c, int x, int y, int w, int h, float v, float range) {
    c.rect(x, y, w, h, BLACK, 2);
    int mid = y + h / 2;
    for (int k = -4; k <= 4; k++) {
        int yy = mid - k * (h / 2) / 5;
        c.hline(x + w, yy, k == 0 ? 14 : 8, BLACK);
    }
    float vv = std::max(-range, std::min(range, v));
    int len = (int)std::lround(vv / range * (h / 2 - 3));
    if (len > 0) c.fillRect(x + 4, mid - len, w - 8, len, BLACK);
    else if (len < 0) c.fillRect(x + 4, mid, w - 8, -len, BLACK, HATCH);
    c.hline(x, mid, w, BLACK);
}

static void drawFlight(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    // grid
    c.vline(270, 40, 340, BLACK); c.vline(271, 40, 340, BLACK);
    c.vline(630, 40, 340, BLACK); c.vline(631, 40, 340, BLACK);
    c.hline(0, 380, W, BLACK); c.hline(0, 381, W, BLACK);
    c.hline(272, 190, 358, BLACK);
    c.hline(272, 285, 358, BLACK);
    c.vline(450, 190, 190, BLACK);
    c.hline(632, 230, 328, BLACK);
    c.vline(480, 382, 158, BLACK); c.vline(481, 382, 158, BLACK);

    // vario column
    varioBar(c, 12, 56, 40, 312, n.vario1s, 5.f);
    label(c, 80, 72, n.altSource == nav::AltSource::Gps       ? "VARIO (GPS)"
                     : n.altSource == nav::AltSource::BlueFly ? "VARIO  BlueFly"
                                                              : "VARIO  m/s");
    c.text(262, 190, fmtSigned1(n.vario1s), std::fabs(n.vario1s) >= 9.95f ? font_l : font_xl, BLACK, RIGHT);
    label(c, 80, 262, TR("średnio 30 s", "30 s average"));
    c.text(262, 330, fmtSigned1(n.varioAvg), font_l, BLACK, RIGHT);

    // altitude column
    label(c, 282, 64, (std::string(TR("WYSOKOŚĆ MSL", "ALTITUDE MSL")) + srcSuffix(n.altSource)).c_str());
    valueUnit(c, 620, 170, fmtInt(n.altMsl), font_xl, "m");
    label(c, 282, 212, TR("nad startem", "above takeoff"));
    valueUnit(c, 440, 272, fmtInt(n.aglTakeoff), font_l, "m");
    label(c, 460, 212, TR("nad terenem", "AGL"));
    valueUnit(c, 620, 272, fmtInt(n.aglTerrain), font_l, "m");
    label(c, 282, 307, TR("PRĘDKOŚĆ GS", "GND SPEED"));
    valueUnit(c, 440, 368, n.fix ? fmtInt(n.gsKmh) : "––", font_l, "km/h");
    label(c, 460, 307, TR("KURS", "TRACK"));
    c.text(620, 368, n.fix ? fmtDeg(n.trackDeg) : "––", font_l, BLACK, RIGHT);

    // home
    label(c, 642, 64, TR("START", "HOME"));
    if (n.homeSet && n.fix) {
        valueUnit(c, 830, 130, fmtDistKm(n.homeDistM), font_l, "km");
        float rel = (float)nav::wrap180(n.homeBrg - n.trackDeg);
        c.circle(895, 120, 52, BLACK, 2);
        c.arrow(895, 120, 90, rel, BLACK);
        std::string ete = TR("czas ", "ETE ") + fmtDur(n.homeEteS) + "   " + fmtDeg(n.homeBrg);
        c.text(642, 210, ete, font_sb, BLACK);
    } else {
        c.text(642, 130, TR("brak", "none"), font_m, BLACK);
    }

    // wind
    label(c, 642, 254, TR("WIATR", "WIND"));
    if (n.wind.valid) {
        valueUnit(c, 830, 320, fmtInt(n.wind.speedKmh), font_l, "km/h");
        // arrow shows where the wind blows TO, relative to track (up = our track)
        float rel = (float)nav::wrap180(n.wind.fromDeg + 180.f - n.trackDeg);
        c.circle(895, 310, 52, BLACK, 2);
        c.arrow(895, 310, 86, rel, BLACK, true);
        std::string fr = TR("z ", "from ") + fmtDeg(n.wind.fromDeg);
        if (n.wind.ageS > 300) fr += "  (" + std::to_string(n.wind.ageS / 60) + " min)";
        c.text(642, 368, fr, font_sb, BLACK);
    } else {
        c.text(642, 312, TR("brak danych", "no data"), font_m, BLACK);
        c.text(642, 360, TR("wykonaj zakręt 360°", "fly a 360° turn"), font_s, BLACK);
    }

    // fuel
    label(c, 10, 404, n.engineOn ? TR("PALIWO – zapas czasu", "FUEL – endurance") : TR("PALIWO – SILNIK WYŁ.", "FUEL – ENGINE OFF"));
    valueUnit(c, 250, 470, fmtDur(n.enduranceMin * 60.f), font_l, "h");
    char b[64];
    std::snprintf(b, sizeof b, "%.1f l", (double)n.fuelLeftL);
    c.text(470, 470, b, font_m, BLACK, RIGHT);
    if (n.homeSet) {
        if (n.returnMarginMin < 0) {
            c.fillRect(6, 488, 468, 46, BLACK);
            c.text(240, 524, TR("WRACAJ – brak paliwa na powrót", "TURN BACK – not enough fuel"), font_sb, WHITE, CENTER);
        } else {
            std::snprintf(b, sizeof b, TR("powrót: OK  zapas +%s", "return: OK  margin +%s"), fmtDur(n.returnMarginMin * 60.f).c_str());
            c.text(10, 522, b, font_sb, BLACK);
        }
    }

    // glide
    label(c, 492, 404, TR("DOSKONAŁOŚĆ L/D", "GLIDE RATIO L/D"));
    int ldw;
    if (!std::isnan(n.ldCurrent)) {
        std::snprintf(b, sizeof b, "%.1f", (double)n.ldCurrent);
        ldw = c.text(492, 470, b, font_l, BLACK);
    } else {
        ldw = c.text(492, 470, "––", font_l, BLACK);
    }
    std::snprintf(b, sizeof b, TR("biegun. %.1f", "polar %.1f"), (double)u.set->glideRatio);
    c.text(492 + ldw + 12, 470, b, font_s, BLACK);
    label(c, 950, 432, TR("dolot do startu", "arrival at home"), RIGHT);
    if (n.homeSet && !std::isnan(n.homeArrivalAgl)) {
        std::snprintf(b, sizeof b, "%+d m", (int)std::lround(n.homeArrivalAgl - u.set->glideMarginM));
        c.text(950, 474, b, font_m, BLACK, RIGHT);
    }
    if (n.homeSet) {
        if (n.homeReachable) {
            c.text(492, 522, TR("✓ DOLECISZ DO STARTU BEZ SILNIKA", "✓ HOME WITHIN GLIDE, ENGINE OFF"), font_sb, BLACK);
        } else {
            c.fillRect(486, 488, 468, 46, BLACK);
            c.text(720, 524, TR("START POZA ZASIĘGIEM ŚLIZGU", "HOME OUT OF GLIDE RANGE"), font_sb, WHITE, CENTER);
        }
    }
}

// ------------------------------------------------------------------ FANET traffic (shared)
// entries worth showing: have a position, heard within the last 60 s
static std::vector<const nav::Traffic*> shownTraffic(const UiContext& u, size_t maxN = 40) {
    std::vector<const nav::Traffic*> v;
    if (!u.traffic) return v;
    for (const auto& t : *u.traffic) {
        if (!t.lastMs || t.ageS > 60) continue;
        v.push_back(&t);
        if (v.size() >= maxN) break;
    }
    return v;
}

static std::string trafficName(const nav::Traffic& t) {
    return t.name.empty() ? "ID " + nav::fanetAddrStr(t.addr) : t.name;
}

static std::string fmtDz(float dz) {
    char b[16];
    dz = std::max(-9999.f, std::min(9999.f, dz));
    std::snprintf(b, sizeof b, "%+d", (int)std::lround(dz));
    return b;
}

// Other aircraft on a map. to(): lat/lon -> screen, rotDeg: map rotation (track-up), clip: map area.
template <class ToScreen>
static void drawTrafficOnMap(Canvas& c, const UiContext& u, ToScreen to, float rotDeg, int x0, int y0, int x1,
                             int y1) {
    auto list = shownTraffic(u);
    std::vector<DebugBox> used;  // label boxes (labels that would overlap are left out)
    // draw the most important last (on top)
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const nav::Traffic& t = **it;
        Pt p = to(t.lat, t.lon);
        if (p.x < x0 - 10 || p.x > x1 + 10 || p.y < y0 - 10 || p.y > y1 + 10) continue;
        if (t.ground) {
            c.fillCircle(p.x, p.y, 10, WHITE);
            c.circle(p.x, p.y, 8, BLACK, 2);
            c.fillRect(p.x - 2, p.y - 2, 5, 5, BLACK);
            continue;
        }
        c.fillCircle(p.x, p.y, 19, WHITE);
        c.arrow(p.x, p.y, 32, t.headingDeg - rotDeg, BLACK, t.stale());
        if (t.level >= 2) c.circle(p.x, p.y, 26, BLACK, t.level >= 3 ? 5 : 3);
        // height difference (+ above us) and climb arrow; name when close or threatening
        std::string l = fmtDz(t.dzM) + (t.climbMs > 0.5f ? "↑" : t.climbMs < -0.5f ? "↓" : "");
        bool named = t.level >= 2 || t.distM < 2000;
        std::string nm = named ? trafficName(t).substr(0, 10) : "";
        int lw = std::max(Canvas::textWidth(l.c_str(), font_s), nm.empty() ? 0 : Canvas::textWidth(nm.c_str(), font_s));
        int lx = p.x + 24 + lw > x1 ? p.x - 24 - lw : p.x + 24;
        int ly1 = p.y + (nm.empty() ? 4 : 28);
        bool clash = false;
        for (auto& b : used)
            if (!(lx + lw < b.x0 || lx > b.x1 || ly1 < b.y0 || p.y - 20 > b.y1)) clash = true;
        if (clash && t.level < 2) continue;
        used.push_back({lx, p.y - 20, lx + lw, ly1, ""});
        c.fillRect(lx - 2, p.y - 20, lw + 4, nm.empty() ? 24 : 48, WHITE);
        c.text(lx, p.y, l, font_s, BLACK);
        if (!nm.empty()) c.text(lx, p.y + 24, nm, font_s, BLACK);
    }
}

// RUCH screen: list on the left, radar (track up) on the right
static const int kTrRowY = 92, kTrRowH = 58, kTrRows = 7, kTrListW = 580;

int trafficRowAt(const UiContext& u, int x, int y) {
    if (x < 120 || x >= kTrListW || y < kTrRowY) return -1;
    int r = (y - kTrRowY) / kTrRowH;
    auto list = shownTraffic(u, kTrRows);
    if (r < 0 || r >= (int)list.size()) return -1;
    // index into u.traffic
    return (int)(list[r] - u.traffic->data());
}

static void drawTraffic(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    const SysInfo* s = u.sys;
    auto list = shownTraffic(u);
    float trk = n.fix ? n.trackDeg : 0.f;
    // ---- list
    c.vline(kTrListW, 40, 470, BLACK);
    c.vline(kTrListW + 1, 40, 470, BLACK);
    label(c, 10, 74, "PILOT");
    label(c, 300, 74, TR("ODL.", "DIST"), RIGHT);
    label(c, 345, 74, TR("KIER.", "BRG"), CENTER);
    label(c, 476, 74, TR("WYS.", "ALT"), RIGHT);
    label(c, 572, 74, "m/s", RIGHT);
    c.hline(0, kTrRowY - 4, kTrListW, BLACK);
    if (list.empty()) {
        c.text(20, 160, TR("Brak innych pilotów", "No other pilots"), font_m, BLACK);
        c.text(20, 196, TR("w zasięgu FANET", "in FANET range"), font_m, BLACK);
        c.text(20, 250, TR("Widać tylko urządzenia FANET", "Only FANET devices are shown"), font_s, BLACK);
        c.text(20, 278, "(Skytraxx, XC Tracer, Naviter, Burnair…).", font_s, BLACK);
        c.text(20, 306, TR("Samoloty i szybowce z samym FLARM", "Aircraft and gliders with FLARM only"), font_s, BLACK);
        c.text(20, 334, TR("nie są widoczne.", "are not visible."), font_s, BLACK);
    }
    for (int i = 0; i < (int)list.size() && i < kTrRows; i++) {
        const nav::Traffic& t = *list[i];
        int y = kTrRowY + i * kTrRowH;
        bool inv = t.level >= 2;
        uint8_t fg = inv ? WHITE : BLACK;
        if (inv) c.fillRect(0, y, kTrListW, kTrRowH - 2, BLACK);
        c.text(10, y + 27, trunc(trafficName(t), font_sb, 200), font_sb, fg);
        const char* types[] = {TR("inny", "other"), TR("paralotnia", "paraglider"), TR("lotnia", "hang glider"), TR("balon", "balloon"), TR("szybowiec", "glider"), TR("samolot", "aircraft"), TR("śmigłowiec", "helicopter"), TR("dron", "drone")};
        char b[48];
        if (t.ground) std::snprintf(b, sizeof b, "%s", TR("na ziemi", "on ground"));
        else std::snprintf(b, sizeof b, "%s", types[t.aircraft & 7]);
        std::string sub = b;
        if (t.ageS >= 10) sub += "  " + std::to_string((int)t.ageS) + " s";
        c.text(10, y + 51, trunc(sub, font_s, 200), font_s, fg);
        c.text(300, y + 40, fmtDistKm(t.distM), font_m, fg, RIGHT);
        // direction relative to our track (up = ahead)
        if (inv) c.fillCircle(345, y + 28, 22, WHITE);
        c.arrow(345, y + 28, 40, (float)nav::wrap180(t.brgDeg - trk), BLACK);
        if (!t.ground) {
            c.text(476, y + 40, fmtDz(t.dzM), font_m, fg, RIGHT);
            c.text(572, y + 40, fmtSigned1(t.climbMs), font_s, fg, RIGHT);
        }
        c.hline(0, y + kTrRowH - 2, kTrListW, BLACK);
    }
    // status line
    {
        std::string st;
        if (!s || s->fanetState == 0) st = TR("FANET wyłączony (Menu > FANET)", "FANET off (Menu > FANET)");
        else if (s->fanetState == 3) st = "FANET: " + (s->fanetErr.empty() ? std::string(TR("błąd radia", "radio error")) : s->fanetErr);
        else {
            st = s->fanetState == 2 ? TR("nadaję", "transmitting") : TR("tylko odbiór", "receive only");
            st += "  ·  ID " + s->fanetId + "  ·  online " + std::string(s->fanetOnline ? TR("TAK", "YES") : TR("NIE", "NO"));
        }
        c.fillRect(0, 512, kTrListW, 28, WHITE);
        c.hline(0, 510, kTrListW, BLACK);
        c.text(10, 534, trunc(st, font_s, kTrListW - 20), font_s, BLACK);
    }
    // ---- radar
    const int cx = 770, cy = 300, R = 190;
    static const float ranges[] = {1000, 2000, 5000, 10000, 20000};
    float far = 0;
    for (size_t i = 0; i < list.size() && i < 5; i++) far = std::max(far, list[i]->distM);
    float range = ranges[4];
    for (float r : ranges)
        if (far <= r * 0.95f) { range = r; break; }
    c.circle(cx, cy, R, BLACK, 2);
    c.circle(cx, cy, R / 2, BLACK, 1, 6);
    char rb[24];
    std::snprintf(rb, sizeof rb, "%g km", (double)range / 1000);
    c.text(W - 8, 72, rb, font_sb, BLACK, RIGHT);
    c.text(W - 8, 532, TR("KURS↑", "TRK↑"), font_s, BLACK, RIGHT);
    // own aircraft: triangle pointing up
    Pt me[3] = {{cx, cy - 16}, {cx - 11, cy + 12}, {cx + 11, cy + 12}};
    c.fillPolygon(me, 3, BLACK);
    std::vector<DebugBox> used;  // label boxes already drawn (skip overlapping labels)
    auto freeBox = [&](int x0, int y0, int x1, int y1) {
        for (auto& b : used)
            if (!(x1 < b.x0 || x0 > b.x1 || y1 < b.y0 || y0 > b.y1)) return false;
        used.push_back({x0, y0, x1, y1, ""});
        return true;
    };
    for (auto it = list.begin(); it != list.end(); ++it) {  // most important first: it keeps its label
        const nav::Traffic& t = **it;
        float d = std::min(t.distM / range, 1.0f) * R;
        float a = (float)((t.brgDeg - trk) * nav::kDeg2Rad);
        int x = cx + (int)std::lround(std::sin(a) * d), y = cy - (int)std::lround(std::cos(a) * d);
        if (t.ground) {
            c.circle(x, y, 7, BLACK, 2);
            continue;
        }
        c.fillCircle(x, y, 15, WHITE);
        c.arrow(x, y, 26, t.headingDeg - trk, BLACK, t.stale());
        if (t.level >= 2) c.circle(x, y, 21, BLACK, t.level >= 3 ? 5 : 3);
        std::string l = fmtDz(t.dzM);
        int tw = Canvas::textWidth(l.c_str(), font_s);
        int lx = x + 18 + tw > W - 4 ? x - 18 - tw : x + 18;
        if (!freeBox(lx - 2, y - 2, lx + tw + 2, y + 22)) continue;
        c.fillRect(lx - 2, y - 2, tw + 4, 24, WHITE);
        c.text(lx, y + 18, l, font_s, BLACK);
    }
}

// Over other screens: the most threatening aircraft at "caution" level (alarm level uses the
// full-width alert banner). Black frame, top right, under the status bar.
void drawTrafficHint(Canvas& c, const UiContext& u) {
    auto list = shownTraffic(u, 1);
    if (list.empty() || list[0]->level < 2) return;
    const nav::Traffic& t = *list[0];
    const NavState& n = *u.nav;
    const int x = W - 330, y = 44, w = 326, h = 118;
    c.fillRect(x, y, w, h, BLACK);
    c.fillRect(x + 5, y + 5, w - 10, h - 10, WHITE);
    float rel = (float)nav::wrap180(t.brgDeg - (n.fix ? n.trackDeg : 0.f));
    c.circle(x + 56, y + 59, 44, BLACK, 2);
    c.arrow(x + 56, y + 59, 72, rel, BLACK);
    char b[48];
    std::snprintf(b, sizeof b, TR("RUCH  godz. %d", "TFC  %d o'clock"), nav::clockPosition(rel));
    c.text(x + 110, y + 34, b, font_sb, BLACK);
    std::string d;
    if (t.distM < 1000) d = std::to_string((int)std::lround(t.distM)) + " m  " + fmtDz(t.dzM) + " m";
    else d = fmtDistKm(t.distM) + " km  " + fmtDz(t.dzM) + " m";
    c.text(x + 110, y + 72, d, font_sb, BLACK);
    c.text(x + 110, y + 104, trunc(trafficName(t), font_s, w - 120), font_s, BLACK);
}

// ------------------------------------------------------------------ MAP
struct MapView {
    nav::LocalProj pr;
    float mpp;      // metres per pixel
    float rotDeg;   // track-up rotation
    int cx, cy;
    Pt toScreen(double lat, double lon) const {
        float x, y;
        pr.toXY(lat, lon, x, y);
        float a = rotDeg * (float)nav::kDeg2Rad, s = std::sin(a), c = std::cos(a);
        float r = x * c - y * s, f = x * s + y * c;
        return {cx + (int)std::lround(r / mpp), cy - (int)std::lround(f / mpp)};
    }
};

static const float kMapRangesKm[] = {1, 2, 5, 10, 20, 50};
static const int kMapRangeCount = 6;

static bool isStrongClass(const nav::Airspace& a) {
    return a.cls == "P" || a.cls == "R" || a.cls == "Q" || a.cls == "CTR" || a.type == "TRA" || a.type == "CTR" ||
           a.name.find("CTR") != std::string::npos;
}

static Pattern mapPattern(const nav::Airspace& a) {
    std::string n = a.name;
    if (a.cls == "P" || a.cls == "R" || a.cls == "Q" || n.rfind("EPP", 0) == 0 || n.rfind("EPR", 0) == 0 ||
        n.rfind("EPD", 0) == 0)
        return MAP_DENSE;
    if (isStrongClass(a) || a.type == "TRA" || a.type == "TSA" || n.rfind("EPTR", 0) == 0 || n.rfind("EPTS", 0) == 0)
        return MAP_MEDIUM;
    return MAP_LIGHT;
}

static void drawHome(Canvas& c, Pt p) {
    Pt roof[3] = {{p.x - 14, p.y - 2}, {p.x, p.y - 16}, {p.x + 14, p.y - 2}};
    c.fillPolygon(roof, 3, BLACK);
    c.fillRect(p.x - 10, p.y - 2, 20, 14, BLACK);
    c.fillRect(p.x - 3, p.y + 4, 6, 8, WHITE);
}

static void drawMap(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    const int MW = 680, MY = 40, MH = 500;
    int idx = std::max(0, std::min(kMapRangeCount - 1, u.set->mapScaleIdx));
    float rangeKm = kMapRangesKm[idx];
    MapView v;
    v.cx = MW / 2;
    v.cy = u.set->mapNorthUp ? MY + MH / 2 : MY + 330;
    v.mpp = rangeKm * 1000.f / (v.cy - MY);
    v.rotDeg = u.set->mapNorthUp ? 0.f : n.trackDeg;
    double lat = n.fix ? n.lat : n.homeLat, lon = n.fix ? n.lon : n.homeLon;
    if (!n.fix && !n.homeSet) {
        c.text(MW / 2, 290, TR("Czekam na GPS…", "Waiting for GPS…"), font_m, BLACK, CENTER);
    } else {
        v.pr.init(lat, lon);
        float viewR = std::hypot((float)MW, (float)MH) * v.mpp;
        // airspaces: pass 1 = hatched interiors (behind everything), pass 2 = outlines + labels
        if (u.asp) {
            const auto& zs = u.asp->zones();
            float dLat = viewR / 111000.f, dLon = dLat / std::max(0.2f, (float)std::cos(lat * nav::kDeg2Rad));
            std::vector<Pt> pts;
            for (size_t i = 0; i < zs.size(); i++) {
                const auto& a = zs[i];
                if (lat < a.minLat - dLat || lat > a.maxLat + dLat || lon < a.minLon - dLon || lon > a.maxLon + dLon) continue;
                if (u.set->isIgnoredClass(a.cls)) continue;
                if (u.asp->activationNow(a, n.utcSec, n.dateYmd) == nav::Activation::Inactive) continue;
                // hatch only zones that matter at the current height (inside or within 2x vertical buffer);
                // zones far above/below are drawn as outline only
                if (!std::isnan(n.altMsl)) {
                    float band = std::max(150.f, 2 * u.set->asVertWarnM);
                    float fl = a.lower.toMsl(n.groundMsl, u.set->qnhHpa), cl = a.upper.toMsl(n.groundMsl, u.set->qnhHpa);
                    if (n.altMsl < fl - band || n.altMsl > cl + band) continue;
                }
                pts.clear();
                for (size_t k = 0; k < a.lat.size(); k++) pts.push_back(v.toScreen(a.lat[k], a.lon[k]));
                c.fillPolygon(pts.data(), (int)pts.size(), BLACK, mapPattern(a));
            }
        }
        if (u.asp) {
            const auto& zs = u.asp->zones();
            float dLat = viewR / 111000.f, dLon = dLat / std::max(0.2f, (float)std::cos(lat * nav::kDeg2Rad));
            std::vector<Pt> pts;
            for (size_t i = 0; i < zs.size(); i++) {
                const auto& a = zs[i];
                if (lat < a.minLat - dLat || lat > a.maxLat + dLat || lon < a.minLon - dLon || lon > a.maxLon + dLon) continue;
                if (u.set->isIgnoredClass(a.cls)) continue;
                pts.clear();
                for (size_t k = 0; k < a.lat.size(); k++) pts.push_back(v.toScreen(a.lat[k], a.lon[k]));
                nav::Activation act = u.asp->activationNow(a, n.utcSec, n.dateYmd);
                bool inactive = act == nav::Activation::Inactive;
                int t = inactive ? 1 : (isStrongClass(a) ? 4 : 2);
                c.polyline(pts.data(), (int)pts.size(), BLACK, t, true, inactive ? 6 : 0);
                // label at the vertex closest to view centre (inside the view)
                int best = -1;
                long bd = 1L << 40;
                for (size_t k = 0; k < pts.size(); k++) {
                    if (pts[k].x < 20 || pts[k].x > MW - 140 || pts[k].y < MY + 20 || pts[k].y > MY + MH - 10) continue;
                    long dx = pts[k].x - v.cx, dy = pts[k].y - v.cy, d = dx * dx + dy * dy;
                    if (d < 90L * 90L) continue;  // keep the label off our own position
                    if (d < bd) { bd = d; best = (int)k; }
                }
                if (best >= 0) {
                    std::string lbl = a.name.substr(0, 14) + " " + a.lower.label();
                    int tw = Canvas::textWidth(lbl.c_str(), font_s);
                    c.fillRect(pts[best].x + 4, pts[best].y - 18, tw + 4, 22, WHITE);
                    c.text(pts[best].x + 6, pts[best].y, lbl, font_s, BLACK);
                }
            }
        }
        // waypoints
        if (u.wpts) {
            int shown = 0;
            for (const auto& w : u.wpts->all()) {
                Pt p = v.toScreen(w.lat, w.lon);
                if (p.x < 0 || p.x > MW || p.y < MY || p.y > MY + MH) continue;
                if (w.isLanding()) { c.rect(p.x - 7, p.y - 7, 14, 14, BLACK, 2); }
                else c.circle(p.x, p.y, 6, BLACK, 2);
                c.text(p.x + 10, p.y + 6, w.name.substr(0, 12), font_s, BLACK);
                if (++shown > 40) break;
            }
        }
        // track
        if (u.fc) {
            const auto& tr = u.fc->track();
            std::vector<Pt> tp;
            tp.reserve(tr.size() + 1);
            for (auto& p : tr) tp.push_back(v.toScreen(p.lat, p.lon));
            if (n.fix) tp.push_back({v.cx, v.cy});
            c.polyline(tp.data(), (int)tp.size(), BLACK, 2);
        }
        // glide ring
        if (u.fc && n.fix && n.phase == nav::Phase::Flying) {
            Pt ring[36];
            for (int i = 0; i < 36; i++) {
                float dir = i * 10.f;
                float r = u.fc->glideRangeM(dir);
                nav::LatLon p = nav::destination({n.lat, n.lon}, dir, r);
                ring[i] = v.toScreen(p.lat, p.lon);
            }
            c.polyline(ring, 36, BLACK, 1, true, 4);
        }
        // home + target line
        if (n.homeSet) drawHome(c, v.toScreen(n.homeLat, n.homeLon));
        if (n.tgtValid && n.fix) {
            Pt t = v.toScreen(n.tgtLat, n.tgtLon);
            c.line(v.cx, v.cy, t.x, t.y, BLACK, 2, 10);
            if (n.tgtKind != nav::TargetKind::Home) c.fillCircle(t.x, t.y, 6, BLACK);
        }
        drawTrafficOnMap(c, u, [&](double la, double lo) { return v.toScreen(la, lo); }, v.rotDeg, 0, MY, MW, MY + MH);
        // own position
        if (n.fix) {
            float a = u.set->mapNorthUp ? n.trackDeg : 0.f;
            c.fillCircle(v.cx, v.cy, 20, WHITE);
            c.arrow(v.cx, v.cy, 40, a, BLACK);
        }
    }
    // frame decorations: north arrow, wind, scale
    {
        // north indicator: circle with "N" and a pointer on the rim
        const int nx = 40, ny = MY + 40, nr = 28;
        c.fillCircle(nx, ny, nr + 2, WHITE);
        c.circle(nx, ny, nr, BLACK, 2);
        c.text(nx, ny + 9, "N", font_sb, BLACK, CENTER);
        float r = (u.set->mapNorthUp ? 0.f : -n.trackDeg) * (float)nav::kDeg2Rad;
        float sx = std::sin(r), cy = std::cos(r);
        Pt tri[3] = {{nx + (int)(sx * (nr + 10)), ny - (int)(cy * (nr + 10))},
                     {nx + (int)(sx * (nr - 4) + cy * 9), ny - (int)(cy * (nr - 4) - sx * 9)},
                     {nx + (int)(sx * (nr - 4) - cy * 9), ny - (int)(cy * (nr - 4) + sx * 9)}};
        c.fillPolygon(tri, 3, BLACK);
    }
    if (n.wind.valid) {
        float rel = u.set->mapNorthUp ? n.wind.fromDeg + 180.f : n.wind.fromDeg + 180.f - n.trackDeg;
        c.fillRect(MW - 120, MY + 6, 114, 80, WHITE);
        c.arrow(MW - 90, MY + 46, 60, rel, BLACK, true);
        c.text(MW - 10, MY + 54, fmtInt(n.wind.speedKmh), font_m, BLACK, RIGHT);
    }
    // scale bar = range/2
    {
        float km = rangeKm / 2;
        int px = (int)(km * 1000.f / v.mpp);
        int y = MY + MH - 14;
        c.fillRect(8, y - 30, px + 70, 40, WHITE);
        c.fillRect(14, y, px, 5, BLACK);
        c.vline(14, y - 8, 13, BLACK);
        c.vline(14 + px, y - 8, 13, BLACK);
        char b[16];
        if (km < 1) std::snprintf(b, sizeof b, "%d m", (int)(km * 1000));
        else std::snprintf(b, sizeof b, km < 10 ? "%.1f km" : "%.0f km", (double)km);
        c.text(20 + px, y + 4, b, font_s, BLACK);
    }
    c.text(MW - 10, MY + MH - 10, u.set->mapNorthUp ? "N↑" : TR("KURS↑", "TRK↑"), font_s, BLACK, RIGHT);

    // side panel
    const int SX = MW + 2;
    c.fillRect(MW, MY, W - MW, MH, WHITE);
    c.vline(MW, MY, MH, BLACK); c.vline(MW + 1, MY, MH, BLACK);
    label(c, SX + 10, MY + 24, TR("WYS. MSL", "ALT MSL"));
    valueUnit(c, W - 10, MY + 74, fmtInt(n.altMsl), font_l, "m");
    label(c, SX + 10, MY + 104, "VARIO");
    valueUnit(c, W - 10, MY + 152, fmtSigned1(n.vario1s), font_l, "m/s");
    label(c, SX + 10, MY + 182, "GS");
    valueUnit(c, W - 10, MY + 222, n.fix ? fmtInt(n.gsKmh) : "––", font_m, "km/h");
    c.hline(MW, MY + 236, W - MW, BLACK);
    c.hline(MW, MY + 237, W - MW, BLACK);
    label(c, SX + 10, MY + 262, TR("NAJBLIŻSZA STREFA", "NEAREST AIRSPACE"));
    if (u.hits && !u.hits->empty() && u.asp && u.hits->front().idx >= 0 &&
        u.hits->front().idx < (int)u.asp->zones().size()) {
        const nav::AirspaceHit& h = u.hits->front();
        const nav::Airspace& a = u.asp->zones()[h.idx];
        c.text(SX + 10, MY + 296, trunc(a.name, font_sb, W - SX - 20), font_sb, BLACK);
        std::string lim = a.cls + "  " + a.lower.label() + " – " + a.upper.label();
        c.text(SX + 10, MY + 324, trunc(lim, font_s, W - SX - 20), font_s, BLACK);
        char b[48];
        if (h.insideH && h.vpos == nav::VertPos::Inside) {
            c.fillRect(SX + 4, MY + 336, W - SX - 10, 52, BLACK);
            c.text((SX + W) / 2, MY + 372, TR("W STREFIE!", "INSIDE!"), font_m, WHITE, CENTER);
        } else if (h.insideH) {
            c.text(SX + 10, MY + 364, h.vpos == nav::VertPos::Below ? TR("pod strefą", "below") : TR("nad strefą", "above"), font_sb, BLACK);
            std::snprintf(b, sizeof b, "%d", (int)h.vertMarginM);
            valueUnit(c, W - 10, MY + 380, b, font_l, "m");
        } else {
            float rel = (float)nav::wrap180(h.bearingDeg - (u.set->mapNorthUp ? 0 : n.trackDeg));
            valueUnit(c, W - 70, MY + 380, fmtDistKm(h.distM), font_l, "km");
            c.arrow(W - 34, MY + 362, 44, rel, BLACK);
        }
        if (h.timeToEntryS >= 0) {
            std::snprintf(b, sizeof b, TR("wlot za %s", "entry in %s"), fmtDur(h.timeToEntryS, false).c_str());
            c.text(SX + 10, MY + 420, b, font_sb, BLACK);
        }
        c.text(SX + 10, MY + 448, activationLabel(u.asp->activationNow(a, n.utcSec, n.dateYmd)), font_s, BLACK);
    } else {
        bool none = u.asp && u.asp->zones().empty();
        c.text(SX + 10, MY + 300, none ? TR("brak danych stref", "no airspace data") : TR("brak stref", "no airspace"), font_sb, BLACK);
        c.text(SX + 10, MY + 328, none ? TR("(karta SD /airspace)", "(SD card /airspace)") : TR("w promieniu 20 km", "within 20 km"), font_s, BLACK);
    }
    char b[24];
    std::snprintf(b, sizeof b, TR("skala %g km", "scale %g km"), (double)rangeKm);
    c.text(W - 10, MY + MH - 10, b, font_s, BLACK, RIGHT);
}

// ------------------------------------------------------------------ TOPO (background map)
namespace {
constexpr int TY = 40, TH = 500;   // map area below the status bar
constexpr int BOXW = 214, BOXH = 92;  // corner data boxes

struct TopoProj {
    int z;
    double ox, oy;  // global pixel of the screen point (0, TY)
    Pt to(double lat, double lon) const {
        double px, py;
        nav::topoToPixel(lat, lon, z, px, py);
        double x = px - ox, y = py - oy + TY;
        x = std::max(-30000.0, std::min(30000.0, x));
        y = std::max(-30000.0, std::min(30000.0, y));
        return {(int)std::lround(x), (int)std::lround(y)};
    }
};
}  // namespace

int topoZoomTapAt(int x, int y) {
    if (x < 120 || x > 840 || y < TY) return 0;  // screen edges = previous / next screen
    return y < TY + TH / 2 ? +1 : -1;
}

static void blitTile(Canvas& c, const uint8_t* bits, int sx, int sy) {
    int r0 = std::max(0, TY - sy), r1 = std::min(256, TY + TH - sy);
    int b0 = std::max(0, -sx) / 8, b1 = std::min(32, (W - sx + 7) / 8);
    for (int r = r0; r < r1; r++) {
        const uint8_t* row = bits + r * 32;
        int y = sy + r;
        for (int b = b0; b < b1; b++) {
            uint8_t v = row[b];
            if (!v) continue;
            int x = sx + b * 8;
            for (int k = 0; k < 8; k++)
                if (v & (0x80 >> k)) c.px(x + k, y, BLACK);
        }
    }
}

// sparse dots = "no map data here"
static void noDataTile(Canvas& c, int sx, int sy) {
    int y0 = std::max(TY, sy), y1 = std::min(TY + TH, sy + 256);
    int x0 = std::max(0, sx), x1 = std::min(W, sx + 256);
    for (int y = y0; y < y1; y++) {
        if ((y - sy) % 16) continue;
        for (int x = x0; x < x1; x++)
            if ((x - sx) % 16 == 8 * (((y - sy) / 16) & 1)) c.px(x, y, BLACK);
    }
}

static void topoBox(Canvas& c, int x, int y) {
    c.fillRect(x, y, BOXW, BOXH, WHITE);
    c.rect(x, y, BOXW, BOXH, BLACK, 2);
}

static void drawTopo(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    nav::TopoView dummy;
    nav::TopoView& v = u.topoView ? *u.topoView : dummy;
    bool havePos = n.fix || n.homeSet;
    double lat = n.fix ? n.lat : n.homeLat, lon = n.fix ? n.lon : n.homeLon;
    int area = (u.topo && havePos) ? u.topo->areaFor(lat, lon) : -1;
    int z = std::max(nav::kTopoMinZoom, std::min(nav::kTopoMaxZoom, u.set->topoZoom));
    if (area >= 0) {
        const nav::TopoArea& a = u.topo->areas()[area];
        if (!a.hasZoom(z)) {  // nearest zoom level that exists in this map
            for (int d = 1; d <= 4; d++) {
                if (a.hasZoom(z - d)) { z -= d; break; }
                if (a.hasZoom(z + d)) { z += d; break; }
            }
        }
    }
    const char* note = nullptr;
    if (!havePos) {
        c.text(W / 2, 290, TR("Czekam na GPS…", "Waiting for GPS…"), font_m, BLACK, CENTER);
        v.valid = false;
        return;
    }
    // view centre: re-centred only when the pilot leaves the middle 60 % of the screen,
    // then placed so that more map is visible ahead (along the track)
    double px, py;
    nav::topoToPixel(lat, lon, z, px, py);
    if (!v.valid || v.z != z || v.area != area || std::fabs(px - v.cx) > W * 0.3 || std::fabs(py - v.cy) > TH * 0.3) {
        v.cx = px;
        v.cy = py;
        if (n.fix && n.gsKmh > 8) {
            float a = n.trackDeg * (float)nav::kDeg2Rad;
            v.cx += std::sin(a) * W * 0.2;
            v.cy -= std::cos(a) * TH * 0.2;
        }
        v.z = z;
        v.area = area;
        v.valid = true;
    }
    TopoProj pr{z, std::floor(v.cx) - W / 2, std::floor(v.cy) - TH / 2};

    // tiles
    if (area >= 0) {
        int tx0 = (int)std::floor(pr.ox / 256), ty0 = (int)std::floor(pr.oy / 256);
        int tx1 = (int)std::floor((pr.ox + W - 1) / 256), ty1 = (int)std::floor((pr.oy + TH - 1) / 256);
        int bad = 0;
        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++) {
                int sx = (int)(tx * 256.0 - pr.ox), sy = (int)(ty * 256.0 - pr.oy) + TY;
                const uint8_t* bits = nullptr;
                nav::TopoTile st = u.topo->tile(area, z, tx, ty, &bits);
                if (st == nav::TopoTile::Ok) blitTile(c, bits, sx, sy);
                else if (st == nav::TopoTile::NoData || st == nav::TopoTile::Error) {
                    noDataTile(c, sx, sy);
                    if (st == nav::TopoTile::Error) bad++;
                }
            }
        if (bad) note = TR("błąd odczytu mapy", "map read error");
    } else {
        note = (u.topo && !u.topo->areas().empty()) ? TR("brak mapy dla tej okolicy", "no map for this area") : TR("brak map (karta SD /maps)", "no maps (SD card /maps)");
    }

    // airspace outlines only (details: STREFY screen + alerts)
    if (u.asp) {
        const auto& zs = u.asp->zones();
        double vlat0, vlon0, vlat1, vlon1;
        nav::topoToLatLon(pr.ox, pr.oy, z, vlat0, vlon0);
        nav::topoToLatLon(pr.ox + W, pr.oy + TH, z, vlat1, vlon1);
        std::vector<Pt> pts;
        for (const auto& a : zs) {
            if (a.minLat > vlat0 || a.maxLat < vlat1 || a.minLon > vlon1 || a.maxLon < vlon0) continue;
            if (u.set->isIgnoredClass(a.cls)) continue;
            bool inactive = u.asp->activationNow(a, n.utcSec, n.dateYmd) == nav::Activation::Inactive;
            pts.clear();
            for (size_t k = 0; k < a.lat.size(); k++) pts.push_back(pr.to(a.lat[k], a.lon[k]));
            // short dashes = airspace (the target line uses long dashes)
            c.polyline(pts.data(), (int)pts.size(), BLACK, inactive ? 1 : (isStrongClass(a) ? 4 : 3), true, inactive ? 4 : 6);
        }
    }
    // waypoints / landings
    if (u.wpts) {
        int shown = 0;
        for (const auto& w : u.wpts->all()) {
            Pt p = pr.to(w.lat, w.lon);
            if (p.x < -10 || p.x > W + 10 || p.y < TY - 10 || p.y > TY + TH + 10) continue;
            if (w.isLanding()) {
                c.fillRect(p.x - 10, p.y - 10, 20, 20, WHITE);
                c.rect(p.x - 8, p.y - 8, 16, 16, BLACK, 3);
            } else {
                c.fillCircle(p.x, p.y, 9, WHITE);
                c.circle(p.x, p.y, 7, BLACK, 3);
            }
            std::string nm = w.name.substr(0, 12);
            int tw = Canvas::textWidth(nm.c_str(), font_s);
            c.fillRect(p.x + 11, p.y - 12, tw + 6, 24, WHITE);
            c.text(p.x + 14, p.y + 8, nm, font_s, BLACK);
            if (++shown > 30) break;
        }
    }
    // flown track
    if (u.fc) {
        const auto& tr = u.fc->track();
        std::vector<Pt> tp;
        tp.reserve(tr.size() + 1);
        for (auto& p : tr) tp.push_back(pr.to(p.lat, p.lon));
        if (n.fix) tp.push_back(pr.to(n.lat, n.lon));
        c.polyline(tp.data(), (int)tp.size(), BLACK, 3);
    }
    Pt me = pr.to(lat, lon);
    if (n.homeSet) {
        Pt h = pr.to(n.homeLat, n.homeLon);
        c.fillCircle(h.x, h.y - 2, 19, WHITE);
        drawHome(c, h);
    }
    if (n.tgtValid && n.fix) {
        Pt t = pr.to(n.tgtLat, n.tgtLon);
        c.line(me.x, me.y, t.x, t.y, BLACK, 3, 20);
        if (n.tgtKind != nav::TargetKind::Home) {
            c.fillCircle(t.x, t.y, 10, WHITE);
            c.fillCircle(t.x, t.y, 7, BLACK);
        }
    }
    drawTrafficOnMap(c, u, [&](double la, double lo) { return pr.to(la, lo); }, 0.f, 0, TY, W, TY + TH);
    if (n.fix) {
        c.fillCircle(me.x, me.y, 26, WHITE);
        c.circle(me.x, me.y, 26, BLACK, 1);
        c.arrow(me.x, me.y, 44, n.trackDeg, BLACK);
    } else {
        c.fillCircle(me.x, me.y, 10, WHITE);
        drawHome(c, me);
    }

    // corner boxes
    // top-left: altitude
    topoBox(c, 0, TY);
    label(c, 8, TY + 36, TR("WYS", "ALT"));
    valueUnit(c, BOXW - 8, TY + 40, fmtInt(n.altMsl), font_m, "m");
    label(c, 8, TY + 80, "AGL");
    valueUnit(c, BOXW - 8, TY + 84, fmtInt(std::isnan(n.aglTerrain) ? n.aglTakeoff : n.aglTerrain), font_m, "m");
    // top-right: speed + track
    topoBox(c, W - BOXW, TY);
    label(c, W - BOXW + 8, TY + 36, "GS");
    valueUnit(c, W - 8, TY + 40, n.fix ? fmtInt(n.gsKmh) : "––", font_m, "km/h");
    label(c, W - BOXW + 8, TY + 80, TR("KURS", "TRACK"));
    c.text(W - 8, TY + 84, n.fix ? fmtDeg(n.trackDeg) : "––", font_m, BLACK, RIGHT);
    // bottom-left: vario + wind
    const int BY = TY + TH - BOXH;
    topoBox(c, 0, BY);
    label(c, 8, BY + 36, "VARIO");
    valueUnit(c, BOXW - 8, BY + 40, fmtSigned1(n.vario1s), font_m, "m/s");
    // wind: hollow arrow = where the wind blows to (north up)
    if (n.wind.valid) {
        c.arrow(30, BY + 68, 36, n.wind.fromDeg + 180.f, BLACK, true);
        valueUnit(c, BOXW - 8, BY + 84, fmtInt(n.wind.speedKmh), font_m, "km/h");
    } else {
        label(c, 8, BY + 80, TR("WIATR", "WIND"));
        c.text(BOXW - 8, BY + 84, "––", font_m, BLACK, RIGHT);
    }
    // bottom-right: nearest airspace
    topoBox(c, W - BOXW, BY);
    if (u.hits && !u.hits->empty() && u.asp && u.hits->front().idx >= 0 &&
        u.hits->front().idx < (int)u.asp->zones().size()) {
        const nav::AirspaceHit& h = u.hits->front();
        const nav::Airspace& a = u.asp->zones()[h.idx];
        c.text(W - BOXW + 8, BY + 30, trunc(a.name, font_s, BOXW - 16), font_s, BLACK);
        if (h.insideH && h.vpos == nav::VertPos::Inside) {
            c.fillRect(W - BOXW + 4, BY + 42, BOXW - 8, 46, BLACK);
            c.text(W - BOXW / 2, BY + 78, TR("W STREFIE!", "INSIDE!"), font_sb, WHITE, CENTER);
        } else if (h.insideH) {
            char b[32];
            std::snprintf(b, sizeof b, "%s %d m", h.vpos == nav::VertPos::Below ? TR("pod", "below") : TR("nad", "above"), (int)h.vertMarginM);
            c.text(W - 8, BY + 80, b, font_sb, BLACK, RIGHT);
        } else {
            c.arrow(W - BOXW + 28, BY + 64, 34, h.bearingDeg, BLACK);
            valueUnit(c, W - 8, BY + 82, fmtDistKm(h.distM), font_m, "km");
        }
    } else {
        label(c, W - BOXW + 8, BY + 36, TR("STREFY", "AIRSPACE"));
        c.text(W - BOXW + 8, BY + 76, (u.asp && u.asp->zones().empty()) ? TR("brak danych", "no data") : TR("brak w pobliżu", "none nearby"), font_sb, BLACK);
    }

    // scale bar + zoom level, bottom centre
    {
        double mpp = nav::topoMetresPerPixel(lat, z);
        if (!(mpp > 0.01)) mpp = 0.01;  // polar / invalid latitude
        static const float steps[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
        float m = steps[0];
        for (float s : steps)
            if (s / mpp <= 190) m = s;
        int len = std::max(2, std::min(190, (int)std::lround(m / mpp)));
        char b[32];
        if (m < 1000) std::snprintf(b, sizeof b, "%d m", (int)m);
        else std::snprintf(b, sizeof b, "%d km", (int)(m / 1000));
        std::string t = std::string(b) + "   N↑  z" + std::to_string(z);
        int tw = Canvas::textWidth(t.c_str(), font_s);
        int bx = BOXW + 14, y = TY + TH - 12;
        c.fillRect(bx - 6, y - 32, len + tw + 30, 44, WHITE);
        c.fillRect(bx, y - 4, len, 6, BLACK);
        c.vline(bx, y - 14, 18, BLACK);
        c.vline(bx + len - 1, y - 14, 18, BLACK);
        c.text(bx + len + 12, y + 2, t, font_s, BLACK);
    }
    if (note) {
        int tw = Canvas::textWidth(note, font_sb);
        c.fillRect(W / 2 - tw / 2 - 12, TY + 6, tw + 24, 40, WHITE);
        c.rect(W / 2 - tw / 2 - 12, TY + 6, tw + 24, 40, BLACK, 2);
        c.text(W / 2, TY + 36, note, font_sb, BLACK, CENTER);
    }
}

// USB mass-storage mode: SD card is a drive on the PC
void drawUsbDisk(Canvas& c, const UiContext& u, bool host) {
    c.clear(WHITE);
    drawStatusBar(c, SCR_SYSTEM, u, TR("TRYB USB", "USB MODE"));
    c.text(W / 2, 130, TR("Karta SD jako dysk USB", "SD card as a USB drive"), font_m, BLACK, CENTER);
    c.text(W / 2, 190, host ? TR("Komputer połączony – kopiuj pliki", "Computer connected – copy the files") : TR("Podłącz kabel USB-C do komputera", "Connect a USB-C cable to a computer"), font_sb, BLACK,
           CENTER);
    const char* lines[] = {TR("mapy podkładowe:  maps/<obszar>/…", "base maps:  maps/<area>/…"),
                                  TR("strefy: airspace/    punkty: waypoints/", "airspace: airspace/    waypoints: waypoints/"),
                                  TR("trasa: route/    teren: terrain/", "route: route/    terrain: terrain/"),
                                  TR("Po skopiowaniu: wysuń (odmontuj) dysk w PC,", "When done: eject the drive on the computer,"),
                                  TR("potem przytrzymaj KEY – powrót i wczytanie danych.", "or hold KEY – back to normal, data reloaded.")};
    int y = 260;
    for (const char* l : lines) {
        c.text(80, y, l, font_sb, BLACK);
        y += 40;
    }
    c.text(W / 2, 520, TR("Zapis lotu wstrzymany – nie używaj tego trybu w locie", "Flight log closed – do not use this mode in flight"), font_s, BLACK, CENTER);
}

// ------------------------------------------------------------------ NAV
static void drawNav(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    const int cx = 265, cy = 292, R = 215;
    c.circle(cx, cy, R, BLACK, 3);
    for (int d = 0; d < 360; d += 10) {
        float a = (float)((d - n.trackDeg) * nav::kDeg2Rad);
        int len = d % 30 == 0 ? 22 : 10;
        for (int k = 0; k < len; k++) {
            int rr = R - 3 - k;
            c.px(cx + (int)(std::sin(a) * rr), cy - (int)(std::cos(a) * rr), BLACK);
            c.px(cx + (int)(std::sin(a) * rr) + 1, cy - (int)(std::cos(a) * rr), BLACK);
        }
        if (d % 90 == 0) {
            static const char* cardinal[] = {"N", "E", "S", "W"};
            c.text(cx + (int)(std::sin(a) * (R - 46)), cy - (int)(std::cos(a) * (R - 46)) + 12, cardinal[d / 90], font_m,
                   BLACK, CENTER);
        }
    }
    // lubber line (our track) at the top
    Pt tri[3] = {{cx - 14, cy - R - 16}, {cx + 14, cy - R - 16}, {cx, cy - R + 6}};
    c.fillPolygon(tri, 3, BLACK);
    if (n.tgtValid && n.fix) {
        float rel = (float)nav::wrap180(n.tgtBrg - n.trackDeg);
        c.arrow(cx, cy, 300, rel, BLACK);
        char b[24];
        if (std::fabs(rel) < 5) std::snprintf(b, sizeof b, "%s", TR("▲ na kursie", "▲ on course"));
        else std::snprintf(b, sizeof b, rel < 0 ? "◄ %d°" : "%d° ►", (int)std::fabs(rel));
        c.text(12, 530, b, font_m, BLACK);
    } else {
        c.text(cx, cy + 10, n.fix ? TR("brak celu", "no target") : TR("brak GPS", "no GPS"), font_m, BLACK, CENTER);
    }

    // right panel
    const int X = 540;
    c.vline(X - 12, 40, 500, BLACK);
    const char* kind = "";
    char kb[32];
    switch (n.tgtKind) {
        case nav::TargetKind::Home: kind = TR("CEL: START (Go Home)", "TARGET: HOME (Go Home)"); break;
        case nav::TargetKind::Waypoint: kind = TR("CEL: PUNKT", "TARGET: WAYPOINT"); break;
        case nav::TargetKind::Landing: kind = TR("CEL: NAJBLIŻSZE LĄDOWISKO", "TARGET: NEAREST LANDING"); break;
        case nav::TargetKind::Point: kind = TR("CEL: PILOT (FANET)", "TARGET: PILOT (FANET)"); break;
        case nav::TargetKind::Route:
            std::snprintf(kb, sizeof kb, TR("TRASA: punkt %d / %d", "ROUTE: point %d / %d"), n.routeIdx + 1, n.routeLen);
            kind = kb;
            break;
    }
    label(c, X, 66, kind);
    c.text(X, 106, trunc(n.tgtValid ? n.tgtName : "––", font_m, W - X - 10), font_m, BLACK);
    label(c, X, 140, TR("ODLEGŁOŚĆ", "DISTANCE"));
    valueUnit(c, W - 10, 216, n.tgtValid ? fmtDistKm(n.tgtDistM) : "––", font_xl, "km");
    c.hline(X, 232, W - X, BLACK);
    // 2x3 grid
    auto cell = [&](int col, int row, const char* l, const std::string& v) {
        int x = X + col * 210, y = 236 + row * 76;
        label(c, x, y + 22, l);
        c.text(x, y + 66, v, font_m, BLACK);
    };
    cell(0, 0, TR("KIERUNEK", "BEARING"), n.tgtValid ? fmtDeg(n.tgtBrg) : "––");
    cell(1, 0, TR("CZAS DOLOTU", "ETE"), fmtDur(n.tgtEteS));
    cell(0, 1, TR("PRZYLOT (lokal.)", "ETA (local)"),
         (n.tgtEteS >= 0 && n.timeValid) ? fmtClock(localSec(u) + (uint32_t)n.tgtEteS) : "––");
    char b[32];
    std::snprintf(b, sizeof b, "%.1f l", (double)n.tgtFuelArriveL);
    cell(1, 1, TR("PALIWO U CELU", "FUEL AT TARGET"), std::isnan(n.tgtFuelArriveL) ? "––" : b);
    std::snprintf(b, sizeof b, "%.1f", (double)n.tgtReqLD);
    cell(0, 2, TR("WYMAGANA L/D", "REQUIRED L/D"), std::isnan(n.tgtReqLD) ? "––" : b);
    std::snprintf(b, sizeof b, "%+d m", (int)std::lround(n.tgtArrivalAgl));
    cell(1, 2, TR("DOLOT ŚLIZGIEM", "FINAL GLIDE"), std::isnan(n.tgtArrivalAgl) ? "––" : b);
    c.text(X, 530, TR("BOOT długo = Go Home", "BOOT long = Go Home"), font_s, BLACK);
}

// ------------------------------------------------------------------ THERMAL
static void drawThermal(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    varioBar(c, 12, 56, 46, 470, n.vario1s, 5.f);
    label(c, 80, 72, n.altSource == nav::AltSource::Gps       ? "VARIO (GPS)"
                     : n.altSource == nav::AltSource::BlueFly ? "VARIO  BlueFly"
                                                              : "VARIO  m/s");
    c.text(470, 210, fmtSigned1(n.vario1s), font_xxl, BLACK, RIGHT);
    c.hline(70, 236, 410, BLACK);
    label(c, 80, 262, TR("średnio 30 s", "30 s average"));
    c.text(470, 320, fmtSigned1(n.varioAvg), font_l, BLACK, RIGHT);
    label(c, 80, 352, n.circling ? TR("KOMIN: zysk / średnio", "THERMAL: gain / avg") : TR("KOMIN (brak krążenia)", "THERMAL (not circling)"));
    char b[48];
    if (n.circling) {
        std::snprintf(b, sizeof b, "%+d m  %+.1f", (int)std::lround(n.thermalGainM), (double)n.thermalAvg);
        c.text(470, 404, b, font_l, BLACK, RIGHT);
    }
    label(c, 80, 440, TR("WYS. / NAD TERENEM", "ALT / AGL"));
    std::string alt = fmtInt(n.altMsl) + " / " + fmtInt(std::isnan(n.aglTerrain) ? n.aglTakeoff : n.aglTerrain) + " m";
    c.text(470, 492, alt, font_m, BLACK, RIGHT);
    if (n.wind.valid) {
        std::snprintf(b, sizeof b, TR("wiatr %d km/h z %s", "wind %d km/h from %s"), (int)std::lround(n.wind.speedKmh), fmtDeg(n.wind.fromDeg).c_str());
        c.text(80, 530, b, font_sb, BLACK);
    }

    // circle plot (track-up), last 90 s
    const int px0 = 490, cx = 725, cy = 290, R = 225;
    c.vline(px0 - 4, 40, 500, BLACK);
    c.circle(cx, cy, R, BLACK, 3, 8);
    c.circle(cx, cy, R / 2, BLACK, 3, 8);
    if (u.fc && n.fix && !u.fc->recent().empty()) {
        const auto& r = u.fc->recent();
        nav::LocalProj pr;
        pr.init(n.lat, n.lon);
        float maxD = 150;
        for (auto& p : r) {
            float x, y;
            pr.toXY(p.lat, p.lon, x, y);
            maxD = std::max(maxD, std::hypot(x, y));
        }
        maxD = std::min(maxD, 1500.f);
        float sc = R / maxD;
            float a = n.trackDeg * (float)nav::kDeg2Rad, s = std::sin(a), co = std::cos(a);
        for (auto& p : r) {
            float x, y;
            pr.toXY(p.lat, p.lon, x, y);
            float rx = x * co - y * s, fy = x * s + y * co;
            int sx = cx + (int)(rx * sc), sy = cy - (int)(fy * sc);
            if (std::hypot((float)(sx - cx), (float)(sy - cy)) > R) continue;
            if (p.vario > 0.2f) c.fillCircle(sx, sy, 3 + (int)std::min(8.f, p.vario * 2.f), BLACK);
            else c.circle(sx, sy, 3, BLACK, 1);
        }
        std::snprintf(b, sizeof b, "Ø %d m", (int)(2 * maxD));
        c.text(W - 10, 530, b, font_s, BLACK, RIGHT);
    }
    c.fillCircle(cx, cy, 16, WHITE);
    c.arrow(cx, cy, 30, 0, BLACK);
    if (n.wind.valid) c.arrow(px0 + 50, 100, 70, n.wind.fromDeg + 180.f - n.trackDeg, BLACK, true);
    label(c, px0 + 10, 64, TR("wiatr", "wind"));
    label(c, W - 10, 64, TR("● = noszenie", "● = lift"), RIGHT);
}

// ------------------------------------------------------------------ AIRSPACE
static void drawAirspace(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    if (!u.asp || u.asp->zones().empty()) {
        c.text(W / 2, 220, TR("Brak danych stref", "No airspace data"), font_l, BLACK, CENTER);
        c.text(W / 2, 270, TR("Wgraj plik OpenAir (*.txt) do katalogu /airspace na karcie SD", "Copy an OpenAir file (*.txt) to /airspace on the SD card"), font_sb, BLACK, CENTER);
        return;
    }
    // table header
    const int rowH = 37, top = 44;
    c.text(10, top + 24, TR("STREFA", "AIRSPACE"), font_s, BLACK);
    c.text(330, top + 24, TR("KL.", "CL."), font_s, BLACK);
    c.text(390, top + 24, TR("DÓŁ – GÓRA", "FLOOR – CEILING"), font_s, BLACK);
    c.text(785, top + 24, TR("ODL.", "DIST"), font_s, BLACK, RIGHT);
    c.text(950, top + 24, "STATUS", font_s, BLACK, RIGHT);
    c.hline(0, top + 32, W, BLACK);
    int rows = 0;
    if (u.hits) {
        for (const auto& h : *u.hits) {
            if (rows >= 7) break;
            if (h.idx < 0 || h.idx >= (int)u.asp->zones().size()) continue;
            const nav::Airspace& a = u.asp->zones()[h.idx];
            int y = top + 34 + rows * rowH;
            c.text(10, y + 27, trunc(a.name, font_sb, 310), font_sb, BLACK);
            c.text(330, y + 27, a.cls.substr(0, 4), font_sb, BLACK);
            c.text(390, y + 27, a.lower.label() + " – " + a.upper.label(), font_sb, BLACK);
            char b[32];
            if (h.insideH) std::snprintf(b, sizeof b, "%s %dm", h.vpos == nav::VertPos::Below ? TR("pod", "below") : TR("nad", "above"), (int)h.vertMarginM);
            else std::snprintf(b, sizeof b, "%s km", fmtDistKm(h.distM).c_str());
            if (h.insideH && h.vpos == nav::VertPos::Inside) std::snprintf(b, sizeof b, "%s", TR("W ŚRODKU", "INSIDE"));
            c.text(785, y + 27, b, font_sb, BLACK, RIGHT);
            if (!h.insideH) {
                float rel = (float)nav::wrap180(h.bearingDeg - n.trackDeg);
                c.arrow(806, y + 18, 26, rel, BLACK);
            }
            std::string st = activationLabel(u.asp->activationNow(a, n.utcSec, n.dateYmd));
            if (h.timeToEntryS >= 0) st = TR("za ", "in ") + fmtDur(h.timeToEntryS, false);
            c.text(950, y + 27, st, font_s, BLACK, RIGHT);
            if (h.level == 2) c.invertRect(0, y, W, rowH - 1);
            else if (h.level == 1) c.rect(0, y, W, rowH - 1, BLACK, 3);
            rows++;
        }
    }
    if (rows == 0) c.text(W / 2, top + 120, TR("Brak stref w promieniu 20 km", "No airspace within 20 km"), font_m, BLACK, CENTER);

    // vertical profile along track (10 km)
    const int PX = 80, PY = 345, PW = 870, PH = 185;
    c.hline(0, PY - 8, W, BLACK);
    c.hline(0, PY - 7, W, BLACK);
    if (!n.fix) {
        c.text(W / 2, PY + 100, TR("Profil: brak GPS", "Profile: no GPS"), font_sb, BLACK, CENTER);
        return;
    }
    const int NS = 60;
    const float distM = 10000.f;
    float ground[NS + 1];
    float gmin = 1e9f, gmax = -1e9f;
    nav::LatLon pts[NS + 1];
    for (int i = 0; i <= NS; i++) {
        pts[i] = nav::destination({n.lat, n.lon}, n.trackDeg, distM * i / NS);
        ground[i] = u.terrain ? u.terrain->elevation(pts[i].lat, pts[i].lon) : NAN;
        if (!std::isnan(ground[i])) { gmin = std::min(gmin, ground[i]); gmax = std::max(gmax, ground[i]); }
    }
    float alt = std::isnan(n.altMsl) ? 0 : n.altMsl;
    float lo = gmin < 1e8f ? std::max(0.f, gmin - 50) : std::max(0.f, alt - 600);
    float hi = std::max(alt + 400.f, lo + 800.f);
    auto Y = [&](float h) { return PY + PH - (int)((h - lo) / (hi - lo) * PH); };
    auto X = [&](int i) { return PX + i * PW / NS; };
    // zones crossing the path
    if (u.hits) {
        for (const auto& h : *u.hits) {
            if (h.distM > distM || h.idx < 0 || h.idx >= (int)u.asp->zones().size()) continue;
            int start = -1;
            for (int i = 0; i <= NS + 1; i++) {
                bool in = i <= NS && u.asp->contains(h.idx, pts[i].lat, pts[i].lon);
                if (in && start < 0) start = i;
                if (!in && start >= 0) {
                    int x0 = X(start), x1 = X(i - 1) + 2;
                    int yt = std::max(PY, Y(std::min(h.ceilMsl, hi)));
                    int yb = std::min(PY + PH, Y(std::max(h.floorMsl, lo)));
                    if (yb > yt) {
                        c.fillRect(x0, yt, x1 - x0, yb - yt, BLACK, h.active ? HATCH : HATCH_SPARSE);
                        c.rect(x0, yt, x1 - x0, yb - yt, BLACK, 2);
                        const auto& a = u.asp->zones()[h.idx];
                        int tw = Canvas::textWidth(a.name.substr(0, 12).c_str(), font_s);
                        c.fillRect(x0 + 2, yt + 2, tw + 6, 22, WHITE);
                        c.text(x0 + 5, yt + 20, a.name.substr(0, 12), font_s, BLACK);
                    }
                    start = -1;
                }
            }
        }
    }
    // terrain
    if (gmin < 1e8f) {
        std::vector<Pt> poly;
        for (int i = 0; i <= NS; i++) poly.push_back({X(i), Y(std::isnan(ground[i]) ? lo : ground[i])});
        poly.push_back({X(NS), PY + PH});
        poly.push_back({X(0), PY + PH});
        c.fillPolygon(poly.data(), (int)poly.size(), BLACK, DOTS);
        c.polyline(poly.data(), NS + 1, BLACK, 2);
    } else {
        c.text(PX + PW / 2, PY + PH - 44, TR("brak mapy terenu (/terrain/*.hgt)", "no terrain data (/terrain/*.hgt)"), font_s, BLACK, CENTER);
    }
    // glide line + own altitude
    float ld = u.set->glideRatio;
    c.line(X(0), Y(alt), X(NS), Y(alt - distM / ld), BLACK, 2, 8);
    Pt me[3] = {{PX - 18, Y(alt) - 10}, {PX - 18, Y(alt) + 10}, {PX, Y(alt)}};
    c.fillPolygon(me, 3, BLACK);
    // axes
    c.vline(PX, PY, PH, BLACK);
    c.hline(PX, PY + PH, PW, BLACK);
    c.text(PX - 4, PY + 18, fmtInt(hi), font_s, BLACK, RIGHT);
    c.text(PX - 4, PY + PH, fmtInt(lo), font_s, BLACK, RIGHT);
    for (int k = 1; k <= 2; k++) {
        int x = PX + PW * k / 2;
        const char* t = k == 1 ? "5 km" : "10 km";
        int tw = Canvas::textWidth(t, font_s);
        c.vline(x - 1, PY + PH - 10, 10, BLACK);
        c.fillRect(x - tw - 8, PY + PH - 30, tw + 6, 22, WHITE);
        c.text(x - 4, PY + PH - 12, t, font_s, BLACK, RIGHT);
    }
}

// ------------------------------------------------------------------ FUEL
FuelButton fuelButtonAt(int x, int y) {
    if (y < 430 || y > 535) return FB_NONE;
    int i = x / 240;
    return (FuelButton)std::max(0, std::min(3, i));
}

static void drawFuel(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    const nav::Settings& s = *u.set;
    label(c, 20, 70, TR("ZAPAS CZASU LOTU", "ENDURANCE"));
    valueUnit(c, 470, 200, fmtDur(n.enduranceMin * 60.f), font_xxl, "h");
    char b[64];
    std::snprintf(b, sizeof b, TR("%.1f l z %.1f l", "%.1f l of %.1f l"), (double)n.fuelLeftL, (double)s.fuelStartL);
    c.text(20, 262, b, font_m, BLACK);
    // gauge
    c.rect(20, 280, 450, 46, BLACK, 3);
    float frac = s.fuelStartL > 0 ? n.fuelLeftL / s.fuelStartL : 0;
    c.fillRect(26, 286, (int)(438 * std::max(0.f, std::min(1.f, frac))), 34, BLACK);
    float resFrac = s.fuelStartL > 0 ? (s.fuelReserveMin / 60.f * s.fuelFlowLph) / s.fuelStartL : 0;
    int rx = 26 + (int)(438 * std::min(1.f, resFrac));
    c.vline(rx, 272, 62, BLACK);
    c.vline(rx + 1, 272, 62, BLACK);
    c.text(rx, 352, TR("rezerwa", "reserve"), font_s, BLACK, CENTER);
    std::snprintf(b, sizeof b, TR("Zużyto: %.2f l", "Used: %.2f l"), (double)n.fuelUsedL);
    c.text(20, 400, b, font_sb, BLACK);
    if (!n.engineOn) c.text(480, 400, TR("licznik stoi", "counter stopped"), font_sb, BLACK, RIGHT);

    c.vline(500, 40, 380, BLACK);
    const int X = 520;
    auto row = [&](int y, const char* l, const std::string& v) {
        label(c, X, y, l);
        c.text(W - 14, y + 4, v, font_m, BLACK, RIGHT);
        c.hline(X, y + 16, W - X - 10, LIGHT);
    };
    std::snprintf(b, sizeof b, "%.1f l/h", (double)s.fuelFlowLph);
    row(84, TR("Zużycie", "Fuel flow"), b);
    std::snprintf(b, sizeof b, "%d min", (int)s.fuelReserveMin);
    row(140, TR("Rezerwa", "Reserve"), b);
    std::snprintf(b, sizeof b, "%d km/h", (int)s.cruiseKmh);
    row(196, TR("Prędkość przelotowa", "Cruise speed"), b);
    std::snprintf(b, sizeof b, "%.1f l", (double)n.returnNeedL);
    row(252, TR("Paliwo na powrót + rez.", "Fuel to home + res."), n.homeSet ? b : "––");
    row(308, TR("Czas powrotu", "Time to home"), n.homeSet ? fmtDur(n.homeEteS) : "––");
    if (n.homeSet) {
        if (n.returnMarginMin < 0) {
            c.fillRect(X, 330, W - X - 10, 80, BLACK);
            c.text((X + W) / 2, 386, TR("WRACAJ!", "TURN BACK!"), font_l, WHITE, CENTER);
        } else {
            label(c, X, 360, TR("Margines powrotu", "Return margin"));
            std::snprintf(b, sizeof b, "+%s", fmtDur(n.returnMarginMin * 60.f).c_str());
            c.text(W - 14, 402, b, font_l, BLACK, RIGHT);
        }
    }
    // touch buttons
    c.hline(0, 424, W, BLACK);
    const char* labels[4] = {"–0.5 l", "+0.5 l", "", ""};
    std::string full = "TANK " + std::to_string((int)std::lround(s.fuelStartL)) + " l";
    for (int i = 0; i < 4; i++) {
        int x = i * 240 + 8;
        c.rect(x, 436, 224, 94, BLACK, 3);
        std::string t = i == 2 ? TR("PEŁNY BAK", "FULL TANK") : (i == 3 ? (n.engineOn ? TR("SILNIK: PRACUJE", "ENGINE: ON") : TR("SILNIK: WYŁ.", "ENGINE: OFF")) : labels[i]);
        c.text(x + 112, 494, t, i < 2 ? font_m : font_sb, BLACK, CENTER);
        if (i == 3 && !n.engineOn) c.invertRect(x + 3, 439, 218, 88);
    }
}

// ------------------------------------------------------------------ SYSTEM
static void drawSystem(Canvas& c, const UiContext& u) {
    const NavState& n = *u.nav;
    const SysInfo& s = *u.sys;
    char b[96];
    int y = 76;
    auto line = [&](int x, const char* l, const std::string& v) {
        int lw = c.text(x, y, l, font_s, BLACK);
        c.text(x + 455, y, trunc(v, font_sb, 455 - lw - 14), font_sb, BLACK, RIGHT);
        y += 31;
    };
    c.text(10, y - 6, TR("BATERIA / GPS / BARO", "BATTERY / GPS / BARO"), font_sb, BLACK);
    y += 30;
    std::snprintf(b, sizeof b, "%d %%  %.2f V", s.battPct, (double)s.battV);
    line(10, TR("Bateria", "Battery"), b);
    std::snprintf(b, sizeof b, "%+d mA %s", s.battMa, s.charging ? TR("(ładowanie)", "(charging)") : "");
    line(10, TR("Prąd", "Current"), b);
    line(10, TR("Czas pracy (szac.)", "Runtime (est.)"), s.battMinLeft >= 0 ? fmtDur(s.battMinLeft * 60.f) : "––");
    line(10, TR("Pomiar baterii", "Battery gauge"), s.battSource);
    line(10, TR("Moduł GPS", "GPS module"), s.gpsModule);
    std::snprintf(b, sizeof b, "%s  sat %d  HDOP %.1f", n.fix ? "3D" : TR("brak", "none"), n.sats, (double)n.hdop);
    line(10, "Fix", b);
    std::snprintf(b, sizeof b, "%.5f  %.5f", n.lat, n.lon);
    line(10, TR("Pozycja", "Position"), b);
    std::snprintf(b, sizeof b, TR("%u / błędy %u", "%u / errors %u"), (unsigned)s.gpsSentences, (unsigned)s.gpsCsErr);
    line(10, TR("Zdania NMEA", "NMEA sentences"), b);
    {
        const char* src = n.altSource == nav::AltSource::Wired     ? TR("kabel", "cable")
                          : n.altSource == nav::AltSource::BlueFly ? "BlueFly"
                                                                    : TR("brak – GPS", "none – GPS");
        if (n.baroOk)
            std::snprintf(b, sizeof b, "%s %.1f hPa", src, (double)n.pressHpa);
        else
            std::snprintf(b, sizeof b, "%s", src);
        line(10, TR("Barometr", "Barometer"), b);
    }
    {
        const char* st[] = {TR("wyłączony", "off"), TR("szukam…", "scanning…"), TR("łączę…", "connecting…"), TR("połączony", "connected"), TR("błąd", "error")};
        std::string v = st[std::max(0, std::min(4, s.bfState))];
        if (s.bfState == 4 && !s.bfError.empty()) v = s.bfError;
        if (s.bfState == 3) {
            char bb[48];
            if (s.bfBattPct >= 0) std::snprintf(bb, sizeof bb, " %d%%", s.bfBattPct);
            else if (s.bfBattV > 0) std::snprintf(bb, sizeof bb, " %.2fV", (double)s.bfBattV);
            else bb[0] = 0;
            v += bb;
            std::snprintf(bb, sizeof bb, " %ddBm", s.bfRssi);
            v += bb;
        }
        line(10, "BlueFly", v);
    }
    {
        const char* ps[] = {TR("wyłączony", "off"), TR("czeka", "waiting"), TR("połączony", "connected"), TR("odbiór", "receiving")};
        std::string v = ps[std::max(0, std::min(3, s.phoneState))];
        if (s.phoneState == 3) v += " " + std::to_string((int)std::lround(s.phoneProgress * 100)) + "%";
        line(10, TR("Telefon", "Phone"), v);
    }
    std::snprintf(b, sizeof b, "%.1f hPa %s", (double)u.set->qnhHpa, n.qnhCalibrated ? TR("(skalibr.)", "(calibr.)") : TR("(domyśl.)", "(default)"));
    line(10, "QNH", b);
    line(10, TR("Wysokość", "Altitude"), "GPS " + (n.fix ? fmtInt(n.gpsAlt) : std::string("–")) + " / baro " +
                             (n.baroOk ? fmtInt(n.altMsl) : std::string("–")) + " m");
    std::snprintf(b, sizeof b, "%d m", (int)std::lround(n.groundMsl));
    line(10, TR("Teren pod Tobą", "Terrain below you"), std::isnan(n.groundMsl) ? "––" : b);

    c.vline(480, 44, 490, BLACK);
    y = 76;
    c.text(495, y - 6, TR("KARTA SD / DANE", "SD CARD / DATA"), font_sb, BLACK);
    y += 30;
    std::snprintf(b, sizeof b, TR("%s  wolne %.0f MB", "%s  free %.0f MB"), s.sdOk ? "OK" : TR("BRAK!", "MISSING!"), (double)s.sdFreeMb);
    line(495, TR("Karta SD", "SD card"), b);
    std::snprintf(b, sizeof b, TR("%d (%d pkt)", "%d (%d pts)"), s.airspaceZones, s.airspacePoints);
    line(495, TR("Strefy", "Airspace"), b);
    if (s.activationEntries >= 0)
        std::snprintf(b, sizeof b, TR("%d wpisów, %u", "%d entries, %u"), s.activationEntries, (unsigned)s.activationDate);
    else
        std::snprintf(b, sizeof b, "%s", TR("brak pliku", "no file"));
    line(495, TR("Aktywacje", "Activations"), b);
    line(495, TR("Punkty / trasa", "Waypoints / route"), std::to_string(s.waypoints) + " / " + std::to_string(s.routePoints));
    {
        std::string v = std::to_string(s.terrainTiles) + " / ";
        if (s.topoAreas == 0) v += TR("brak map", "no maps");
        else if (!s.topoArea.empty()) v += s.topoArea + (s.topoAreas > 1 ? " +" + std::to_string(s.topoAreas - 1) : "");
        else v += std::to_string(s.topoAreas) + TR(" obsz.", " areas");
        line(495, TR("Teren / mapa", "Terrain / map"), v);
    }
    {
        std::string fn = s.logFile.substr(s.logFile.find_last_of('/') + 1);
        if (fn.size() > 15) fn = fn.substr(fn.size() - 10);  // "NAV-01.igc"
        line(495, TR("Zapis lotu", "Flight log"), s.recording ? fn : TR("nie", "no"));
    }
    std::snprintf(b, sizeof b, "%u / %u kB", (unsigned)s.freeHeapKb, (unsigned)s.freePsramKb);
    line(495, TR("RAM / PSRAM wolne", "RAM / PSRAM free"), b);
    {
        std::string v;
        if (s.fanetState == 0) v = TR("wyłączony", "off");
        else if (s.fanetState == 3) v = s.fanetErr.empty() ? TR("błąd", "error") : s.fanetErr;
        else {
            std::snprintf(b, sizeof b, "%s tx %u rx %u", s.fanetId.c_str(), (unsigned)s.fanetTxCount,
                          (unsigned)s.fanetRxCount);
            v = b;
        }
        line(495, "FANET", v);
    }
    line(495, TR("Wersja", "Version"), s.version);
    line(495, TR("Z telefonu", "From phone"), s.phoneLast.empty() ? "–" : s.phoneLast);
    c.text(495, y, "I2C:", font_s, BLACK);
    c.text(950, y, trunc(s.i2cDevices, font_s, 390), font_s, BLACK, RIGHT);
    y += 31;
    for (size_t i = 0; i < s.loadErrors.size() && i < 3 && y < H - 4; i++) {
        c.text(495, y, trunc(s.loadErrors[i], font_s, 450), font_s, BLACK);
        y += 26;
    }
}

// ------------------------------------------------------------------ public
void drawScreen(Canvas& c, ScreenId s, const UiContext& u) {
    c.clear(WHITE);
    switch (s) {
        case SCR_FLIGHT: drawFlight(c, u); break;
        case SCR_MAP: drawMap(c, u); break;
        case SCR_TOPO: drawTopo(c, u); break;
        case SCR_TRAFFIC: drawTraffic(c, u); break;
        case SCR_NAV: drawNav(c, u); break;
        case SCR_THERMAL: drawThermal(c, u); break;
        case SCR_AIRSPACE: drawAirspace(c, u); break;
        case SCR_FUEL: drawFuel(c, u); break;
        case SCR_SYSTEM: drawSystem(c, u); break;
        default: break;
    }
    drawStatusBar(c, s, u);
}

void drawAlertBanner(Canvas& c, const UiContext& u, int id) {
    if (!u.alerts || id < 0) return;
    const nav::Alert& a = u.alerts->get(id);
    c.fillRect(0, 40, W, 124, BLACK);
    c.fillRect(6, 46, W - 12, 112, WHITE);
    c.fillRect(12, 52, W - 24, 100, BLACK);
    c.text(30, 100, trunc(a.title, font_l, 760), font_l, WHITE);
    c.text(30, 140, trunc(a.detail, font_sb, 760), font_sb, WHITE);
    c.text(W - 30, 92, TR("KLAWISZ", "KEY"), font_sb, WHITE, RIGHT);
    c.text(W - 30, 130, TR("= potwierdź", "= acknowledge"), font_sb, WHITE, RIGHT);
}

void drawSummary(Canvas& c, const UiContext& u) {
    const auto& f = u.fc->lastFlight;
    c.clear(WHITE);
    drawStatusBar(c, SCR_FLIGHT, u, TR("PODSUMOWANIE", "SUMMARY"));
    c.text(W / 2, 110, TR("LOT ZAKOŃCZONY", "FLIGHT COMPLETED"), font_l, BLACK, CENTER);
    char b[64];
    int y = 180;
    auto row = [&](const char* l, const std::string& v) {
        c.text(130, y, l, font_sb, BLACK);
        c.text(830, y, v, font_m, BLACK, RIGHT);
        y += 46;
    };
    row(TR("Czas lotu", "Flight time"), fmtDur((float)f.durationS));
    std::snprintf(b, sizeof b, "%d m", (int)std::lround(f.maxAlt));
    row(TR("Maks. wysokość", "Max altitude"), b);
    std::snprintf(b, sizeof b, "%+.1f / %+.1f m/s", (double)f.maxClimb, (double)f.maxSink);
    row(TR("Maks. wznoszenie / opadanie", "Max climb / sink"), b);
    std::snprintf(b, sizeof b, "%.1f km", (double)f.distKm);
    row(TR("Przebyta trasa", "Distance flown"), b);
    std::snprintf(b, sizeof b, "%.1f km", (double)f.maxDistHomeKm);
    row(TR("Maks. odległość od startu", "Max distance from takeoff"), b);
    std::snprintf(b, sizeof b, "%.1f l", (double)f.fuelUsedL);
    row(TR("Zużyte paliwo (szac.)", "Fuel used (est.)"), b);
    if (u.sys && !u.sys->logFile.empty()) c.text(W / 2, 500, u.sys->logFile, font_s, BLACK, CENTER);
    c.text(W / 2, 530, TR("Naciśnij klawisz, aby wrócić", "Press a key to return"), font_s, BLACK, CENTER);
}

void drawTransferBanner(Canvas& c, const UiContext& u) {
    if (!u.sys || u.sys->phoneState != 3) return;
    const int y = H - 70;
    c.fillRect(0, y, W, 70, WHITE);
    c.rect(0, y, W, 70, BLACK, 3);
    char b[32];
    std::snprintf(b, sizeof b, "%d%%", (int)std::lround(u.sys->phoneProgress * 100));
    c.text(16, y + 30, TR("Odbieranie z telefonu:", "Receiving from phone:"), font_sb, BLACK);
    c.text(W - 16, y + 30, b, font_sb, BLACK, RIGHT);
    c.text(16 + Canvas::textWidth(TR("Odbieranie z telefonu: ", "Receiving from phone: "), font_sb), y + 30, trunc(u.sys->phoneFile, font_sb, 560),
           font_sb, BLACK);
    c.rect(16, y + 42, W - 32, 18, BLACK, 2);
    c.fillRect(19, y + 45, (int)((W - 38) * std::max(0.f, std::min(1.f, u.sys->phoneProgress))), 12, BLACK);
}

void drawPowerHint(Canvas& c) {
    const int x = 180, y = 190, w = 600, h = 160;
    c.fillRect(x, y, w, h, WHITE);
    c.rect(x, y, w, h, BLACK, 6);
    c.text(W / 2, y + 70, TR("WYŁĄCZANIE…", "POWERING OFF…"), font_l, BLACK, CENTER);
    c.text(W / 2, y + 115, TR("trzymaj oba przyciski jeszcze 2 s", "keep holding both buttons for 2 s"), font_sb, BLACK, CENTER);
    c.text(W / 2, y + 145, TR("puść, aby anulować", "release to cancel"), font_s, BLACK, CENTER);
}

void drawPowerOff(Canvas& c, const char* line1, const char* line2) {
    c.clear(WHITE);
    c.text(W / 2, 200, "ParaNav", font_xl, BLACK, CENTER);
    c.text(W / 2, 300, line1, font_l, BLACK, CENTER);
    c.text(W / 2, 360, line2, font_sb, BLACK, CENTER);
}

void drawBoot(Canvas& c, const std::vector<std::string>& lines, const char* version) {
    c.clear(WHITE);
    c.text(W / 2, 110, "ParaNav", font_xl, BLACK, CENTER);
    c.text(W / 2, 150, std::string(TR("komputer motoparalotniowy  v", "paramotor flight computer  v")) + version, font_sb, BLACK, CENTER);
    int y = 210;
    for (auto& l : lines) {
        c.text(80, y, trunc(l, font_sb, W - 160), font_sb, BLACK);
        y += 32;
        if (y > 530) break;
    }
}

}  // namespace ui
