#include "flightlog.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "geo.h"
#include "terrain.h"
#include "yield.h"

namespace nav {

namespace {

int num(const char* s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

bool endsWithIgc(const char* n) {
    size_t l = std::strlen(n);
    return l > 4 && (std::strcmp(n + l - 4, ".igc") == 0 || std::strcmp(n + l - 4, ".IGC") == 0);
}

long fileSize(FILE* f) {
    long cur = std::ftell(f);
    if (std::fseek(f, 0, SEEK_END) != 0) return -1;
    long s = std::ftell(f);
    std::fseek(f, cur, SEEK_SET);
    return s;
}

// Limits for the spike filter (well above what a paraglider / paramotor can do, and a car ride too)
constexpr float kMaxVzMs = 30.f;         // vertical speed
constexpr double kMaxSpeedMs = 300 / 3.6; // ground speed
constexpr int kSettleS = 15;             // a jump that stays this many records is real

}  // namespace

bool parseIgcB(const char* l, IgcFix& f) {
    // B HHMMSS DDMMmmm N DDDMMmmm E V PPPPP GGGGG
    if (!l || l[0] != 'B' || std::strlen(l) < 35) return false;
    int hh = num(l + 1, 2), mm = num(l + 3, 2), ss = num(l + 5, 2);
    int la = num(l + 7, 2), lam = num(l + 9, 5), lo = num(l + 15, 3), lom = num(l + 18, 5);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59 || la < 0 || lam < 0 || lo < 0 || lom < 0) return false;
    char ns = l[14], ew = l[23], v = l[24];
    if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return false;
    f.sec = hh * 3600 + mm * 60 + ss;
    f.lat = la + lam / 60000.0;
    f.lon = lo + lom / 60000.0;
    if (ns == 'S') f.lat = -f.lat;
    if (ew == 'W') f.lon = -f.lon;
    f.valid = v == 'A';
    auto alt = [](const char* s) {  // 5 chars, may start with '-'
        bool neg = s[0] == '-';
        int a = num(s + (neg ? 1 : 0), neg ? 4 : 5);
        return a < 0 ? 0 : (neg ? -a : a);
    };
    f.pressAlt = alt(l + 25);
    f.gpsAlt = alt(l + 30);
    return true;
}

bool parseIgcDate(const char* l, int& y, int& m, int& d) {
    if (!l || std::strncmp(l, "HFDTE", 5) != 0) return false;
    const char* p = l + 5;
    if (std::strncmp(p, "DATE:", 5) == 0) p += 5;
    int dd = num(p, 2), mo = num(p + 2, 2), yy = num(p + 4, 2);
    if (dd < 1 || dd > 31 || mo < 1 || mo > 12 || yy < 0) return false;
    d = dd;
    m = mo;
    y = 2000 + yy;
    return true;
}

bool flightInfo(const std::string& path, FlightFileInfo& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.path = path;
    size_t sl = path.find_last_of('/');
    out.name = sl == std::string::npos ? path : path.substr(sl + 1);
    out.bytes = fileSize(f);
    char line[128];
    IgcFix fx;
    int first = -1;
    // header + first fix: within the first few kB
    for (int i = 0; i < 80 && std::fgets(line, sizeof line, f); i++) {
        parseIgcDate(line, out.year, out.month, out.day);
        if (parseIgcB(line, fx)) {
            first = fx.sec;
            break;
        }
    }
    // last fix: in the last ~2 kB
    int last = first;
    if (first >= 0 && out.bytes > 0) {
        std::fseek(f, std::max(0L, out.bytes - 2048), SEEK_SET);
        if (out.bytes > 2048 && !std::fgets(line, sizeof line, f)) line[0] = 0;  // skip the partial line
        while (std::fgets(line, sizeof line, f))
            if (parseIgcB(line, fx)) last = fx.sec;
    }
    std::fclose(f);
    out.startSec = first;
    out.durationS = first < 0 ? 0 : (last - first + 86400) % 86400;
    return true;
}

std::vector<FlightFileInfo> listFlights(const std::string& dir, size_t maxFiles) {
    std::vector<FlightFileInfo> v;
    DIR* d = opendir(dir.c_str());
    if (!d) return v;
    std::vector<std::string> names;
    while (dirent* e = readdir(d))
        if (e->d_name[0] != '.' && endsWithIgc(e->d_name)) names.push_back(e->d_name);
    closedir(d);
    // file names start with the date (YYYY-MM-DD-...-NN): newest first, the slow part runs on maxFiles only
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a > b; });
    if (names.size() > maxFiles) names.resize(maxFiles);
    for (auto& n : names) {
        maybeYield(1);
        FlightFileInfo fi;
        if (flightInfo(dir + "/" + n, fi)) v.push_back(fi);
    }
    std::stable_sort(v.begin(), v.end(), [](const FlightFileInfo& a, const FlightFileInfo& b) {
        long ka = (long)a.year * 10000 + a.month * 100 + a.day, kb = (long)b.year * 10000 + b.month * 100 + b.day;
        if (ka != kb) return ka > kb;
        if (a.startSec != b.startSec) return a.startSec > b.startSec;
        return a.name > b.name;
    });
    return v;
}

bool loadFlight(const std::string& path, FlightLog& out, size_t maxPoints, std::string* err) {
    out = FlightLog();
    out.path = path;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open";
        return false;
    }
    long size = fileSize(f);
    // ~37 bytes per B record: keep every step-th fix
    size_t est = size > 0 ? (size_t)(size / 37) + 1 : 1;
    size_t step = std::max<size_t>(1, (est + maxPoints - 1) / std::max<size_t>(1, maxPoints));
    out.pts.reserve(std::min(est / step + 4, maxPoints + 4));  // one block (PSRAM fragmentation)

    char line[128];
    // pass 1: date and the barometer offset (pressure altitude is QNE; level it to GNSS altitude)
    // (median of the first 120 differences: a broken record does not shift it)
    int offs[120];
    int offN = 0;
    bool anyPress = false;
    IgcFix fx;
    while (std::fgets(line, sizeof line, f)) {
        parseIgcDate(line, out.year, out.month, out.day);
        if (!parseIgcB(line, fx) || !fx.valid) continue;
        if (fx.pressAlt != 0) anyPress = true;
        if (fx.pressAlt != 0 && fx.gpsAlt != 0 && offN < 120) offs[offN++] = fx.gpsAlt - fx.pressAlt;
        if (offN >= 120) break;
    }
    out.baro = anyPress;
    float off = 0.f;
    if (offN) {
        std::sort(offs, offs + offN);
        off = (float)offs[offN / 2];
    }
    std::fseek(f, 0, SEEK_SET);

    // pass 2: points + statistics from every fix
    struct R {
        int t;
        double lat, lon;
        float alt;
        bool altHeld = false, posHeld = false;  // value held by the spike filter (not measured)
    };
    R ring[64];
    int rn = 0, rh = 0;  // count, head
    int first = -1, prevSec = -1, dayAdd = 0;
    size_t k = 0;
    R anchor{0, 0, 0, 0, false, false};
    bool haveAnchor = false;
    LatLon start;
    FlightStats& s = out.st;
    int lastT = 0;
    // spike filter state: last good values, candidate new level (altitude / position)
    R good{0, 0, 0, 0, false, false};  // t = time of the last good altitude
    int goodPosT = 0;    // time of the last good position
    bool haveGood = false;
    struct Cand {
        int n;
        float alt;
        double lat, lon;
    };
    Cand candA{0, 0, 0, 0}, candP{0, 0, 0, 0};
    while (std::fgets(line, sizeof line, f)) {
        maybeYield(300);  // long files: let the watchdog breathe
        if (!parseIgcB(line, fx) || !fx.valid) continue;
        if (prevSec >= 0 && fx.sec < prevSec - 3600) dayAdd += 86400;  // past midnight UTC
        prevSec = fx.sec;
        int abs = fx.sec + dayAdd;
        if (first < 0) {
            first = abs;
            out.startSec = fx.sec;
            start = {fx.lat, fx.lon};
            out.minLat = out.maxLat = fx.lat;
            out.minLon = out.maxLon = fx.lon;
        }
        int t = abs - first;
        bool altHeld = false, posHeld = false;
        // altitude: barometric levelled to GNSS; a record without pressure altitude falls back to GNSS
        // (comparable after the levelling); neither = no altitude in this record
        bool altRead = fx.pressAlt != 0 || fx.gpsAlt != 0;
        float alt = anyPress && fx.pressAlt != 0 ? fx.pressAlt + off : (float)fx.gpsAlt;
        double la = fx.lat, lo = fx.lon;
        // Physically impossible jumps (broken records, GPS glitches) are not used: the last good
        // value is held instead. A new level that stays consistent for kSettleS is accepted (a
        // real change after a gap in the log).
        if (!haveGood) {
            good = {t, la, lo, alt, false, false};
            goodPosT = t;
            haveGood = true;
        } else {
            // allowed change grows with the time since the last good value
            int dtA = std::max(1, t - good.t), dtP = std::max(1, t - goodPosT);
            bool altBad = !altRead || std::fabs(alt - good.alt) > kMaxVzMs * dtA + 20.f;
            bool posBad = distanceM({good.lat, good.lon}, {la, lo}) > kMaxSpeedMs * dtP + 200.0;
            bool shifted = false;
            auto settle = [&](bool bad, Cand& cd, float v, double v2, double tolAlt, double tolPos) {
                if (!bad) {
                    cd.n = 0;
                    return false;
                }
                bool same = cd.n > 0 && (tolAlt > 0 ? std::fabs(v - cd.alt) <= tolAlt
                                                    : distanceM({cd.lat, cd.lon}, {v2, (double)v}) <= tolPos);
                cd = {same ? cd.n + 1 : 1, v, v2, (double)v};  // follows the new level while it settles
                if (cd.n < kSettleS) return true;               // still bad until it settles
                cd.n = 0;
                shifted = true;  // a real jump (gap in the log): it must not count as climb / distance
                return false;
            };
            altBad = settle(altBad && altRead, candA, alt, 0, kMaxVzMs * 2 + 20.0, 0) || (!altRead);
            posBad = settle(posBad, candP, (float)lo, la, 0, 300.0);
            if (altBad) {
                alt = good.alt;
                altHeld = true;
                s.rejected++;
            }
            if (posBad) {
                la = good.lat;
                lo = good.lon;
                posHeld = true;
                if (!altBad) s.rejected++;
            }
            if (!altBad) good.alt = alt, good.t = t;
            if (!posBad) good.lat = la, good.lon = lo, goodPosT = t;
            if (shifted) {
                rn = 0;              // climb / speed windows start again
                haveAnchor = false;  // the jump is not track length
            }
        }
        if (out.pts.empty()) s.takeoffAlt = s.maxAlt = s.minAlt = alt;
        s.maxAlt = std::max(s.maxAlt, alt);
        s.minAlt = std::min(s.minAlt, alt);
        out.minLat = std::min(out.minLat, la);
        out.maxLat = std::max(out.maxLat, la);
        out.minLon = std::min(out.minLon, lo);
        out.maxLon = std::max(out.maxLon, lo);
        s.maxDistKm = std::max(s.maxDistKm, (float)(distanceM(start, {la, lo}) / 1000.0));
        // track length in >= 5 s steps (GPS jitter does not add up)
        if (!haveAnchor) {
            anchor = {t, la, lo, alt};
            haveAnchor = true;
        } else if (t - anchor.t >= 5) {
            s.distKm += (float)(distanceM({anchor.lat, anchor.lon}, {la, lo}) / 1000.0);
            anchor = {t, la, lo, alt};
        }
        // climb / sink / speed over the last >= 10 s
        for (int i = 1; i <= rn; i++) {
            const R& o = ring[(rh - i + 64) % 64];
            int dt = t - o.t;
            if (dt < 10) continue;
            if (dt <= 30) {  // held values are not measurements: no rate from them
                if (!altHeld && !o.altHeld) {
                    float v = (alt - o.alt) / dt;
                    s.maxClimb = std::max(s.maxClimb, v);
                    s.maxSink = std::min(s.maxSink, v);
                }
                if (!posHeld && !o.posHeld) {
                    float sp = (float)(distanceM({o.lat, o.lon}, {la, lo}) / dt * 3.6);
                    s.maxSpeedKmh = std::max(s.maxSpeedKmh, sp);
                }
            }
            break;
        }
        ring[rh] = {t, la, lo, alt, altHeld, posHeld};
        rh = (rh + 1) % 64;
        rn = std::min(rn + 1, 64);
        if (k++ % step == 0 || out.pts.empty())
            out.pts.push_back({t, (float)la, (float)lo, (int16_t)std::lround(std::max(-500.f, std::min(30000.f, alt)))});
        lastT = t;
    }
    std::fclose(f);
    if (first < 0) {
        if (err) *err = "no fixes";
        return false;
    }
    // always keep the last fix (landing)
    if (out.pts.back().t != lastT) {
        const R& o = ring[(rh - 1 + 64) % 64];
        out.pts.push_back({o.t, (float)o.lat, (float)o.lon, (int16_t)std::lround(o.alt)});
    }
    s.durationS = lastT;
    s.avgSpeedKmh = lastT > 0 ? s.distKm / (lastT / 3600.f) : 0;
    return true;
}

void computeGround(FlightLog& log, Terrain& terrain) {
    log.ground.assign(FlightLog::kGroundN, FlightLog::kNoGround);
    if (log.pts.empty()) return;
    int dur = std::max(1, log.st.durationS);
    size_t j = 0;
    for (int i = 0; i < FlightLog::kGroundN; i++) {
        int t = (int)((int64_t)i * dur / (FlightLog::kGroundN - 1));
        while (j + 1 < log.pts.size() && log.pts[j + 1].t <= t) j++;
        float g = terrain.elevation(log.pts[j].lat, log.pts[j].lon);
        if (!std::isnan(g)) log.ground[i] = (int16_t)std::lround(std::max(-500.f, std::min(9000.f, g)));
        maybeYield(10);
    }
}

bool deleteFlight(const std::string& igcPath) {
    if (std::remove(igcPath.c_str()) != 0) return false;
    std::string gpx = igcPath.size() > 4 ? igcPath.substr(0, igcPath.size() - 4) + ".gpx" : "";
    if (!gpx.empty()) std::remove(gpx.c_str());
    return true;
}

}  // namespace nav
