#include "fanet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "geo.h"

namespace nav {

// ---------------------------------------------------------------- encoding helpers
static void putHeader(uint8_t* b, uint8_t type, uint32_t addr) {
    b[0] = type & 0x3F;  // no extended header, no forward
    b[1] = (uint8_t)(addr >> 16);
    b[2] = (uint8_t)(addr & 0xFF);  // unique id, little endian
    b[3] = (uint8_t)((addr >> 8) & 0xFF);
}

static void putCoord(uint8_t* b, double lat, double lon) {
    int32_t la = (int32_t)std::lround(lat * 93206.0);
    int32_t lo = (int32_t)std::lround(lon * 46603.0);
    b[0] = (uint8_t)la;
    b[1] = (uint8_t)(la >> 8);
    b[2] = (uint8_t)(la >> 16);
    b[3] = (uint8_t)lo;
    b[4] = (uint8_t)(lo >> 8);
    b[5] = (uint8_t)(lo >> 16);
}

static int32_t s24(const uint8_t* b) {
    int32_t v = b[0] | (b[1] << 8) | (b[2] << 16);
    if (v & 0x800000) v |= ~0xFFFFFF;
    return v;
}

// 7-bit value with "x5" scale flag in bit 7; unit = units per LSB
static uint8_t scaled7u(float v, float unit) {
    int r = (int)std::lround(v / unit);
    if (r < 0) r = 0;
    if (r <= 127) return (uint8_t)r;
    r = (int)std::lround(v / (unit * 5));
    return (uint8_t)(0x80 | std::min(r, 127));
}
static uint8_t scaled7s(float v, float unit) {
    int r = (int)std::lround(v / unit);
    if (r >= -64 && r <= 63) return (uint8_t)(r & 0x7F);
    r = (int)std::lround(v / (unit * 5));
    r = std::max(-64, std::min(63, r));
    return (uint8_t)(0x80 | (r & 0x7F));
}
static float unscaled7u(uint8_t b, float unit) { return (b & 0x7F) * unit * ((b & 0x80) ? 5 : 1); }
static float unscaled7s(uint8_t b, float unit) {
    int v = b & 0x7F;
    if (v & 0x40) v -= 128;
    return v * unit * ((b & 0x80) ? 5 : 1);
}

size_t fanetEncodeTracking(uint8_t* b, size_t cap, uint32_t addr, const FanetOwn& o) {
    if (cap < 15) return 0;
    putHeader(b, FT_TRACKING, addr);
    putCoord(b + 4, o.lat, o.lon);
    int alt = (int)std::lround(std::max(0.f, o.altM));
    uint16_t t = 0;
    if (alt > 2047) t = (uint16_t)(0x0800 | std::min(2047, (alt + 2) / 4));
    else t = (uint16_t)alt;
    t |= (uint16_t)((o.aircraft & 7) << 12);
    if (o.online) t |= 0x8000;
    b[10] = (uint8_t)t;
    b[11] = (uint8_t)(t >> 8);
    b[12] = scaled7u(o.speedKmh, 0.5f);
    b[13] = scaled7s(o.climbMs, 0.1f);
    b[14] = (uint8_t)((int)std::lround(wrap360(o.headingDeg) * 256.0 / 360.0) & 0xFF);
    return 15;
}

size_t fanetEncodeGround(uint8_t* b, size_t cap, uint32_t addr, double lat, double lon, uint8_t gtype, bool online) {
    if (cap < 11) return 0;
    putHeader(b, FT_GROUND, addr);
    putCoord(b + 4, lat, lon);
    b[10] = (uint8_t)(((gtype & 0x0F) << 4) | (online ? 1 : 0));
    return 11;
}

// Polish letters -> ASCII (other FANET devices do not agree on a character set)
static std::string asciiName(const std::string& in) {
    static const char* from[] = {"ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż", "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ź", "Ż"};
    static const char to[] = "acelnoszzACELNOSZZ";
    std::string out;
    for (size_t i = 0; i < in.size();) {
        bool done = false;
        for (int k = 0; k < 18 && !done; k++) {
            size_t L = std::char_traits<char>::length(from[k]);
            if (in.compare(i, L, from[k]) == 0) {
                out += to[k];
                i += L;
                done = true;
            }
        }
        if (done) continue;
        unsigned char c = (unsigned char)in[i++];
        if (c >= 32 && c < 127) out += (char)c;
        else if (c >= 0xC0) out += '?';  // other multi-byte character: one '?'
    }
    return out;
}

size_t fanetEncodeName(uint8_t* b, size_t cap, uint32_t addr, const std::string& utf8) {
    std::string name = asciiName(utf8);
    size_t n = std::min(name.size(), (size_t)20);
    if (cap < 4 + n || n == 0) return 0;
    putHeader(b, FT_NAME, addr);
    for (size_t i = 0; i < n; i++) b[4 + i] = (uint8_t)name[i];
    return 4 + n;
}

bool fanetDecode(const uint8_t* b, size_t len, FanetPacket& p) {
    p = FanetPacket();
    if (len < 4) return false;
    p.type = b[0] & 0x3F;
    p.forward = b[0] & 0x40;
    p.src = ((uint32_t)b[1] << 16) | b[2] | ((uint32_t)b[3] << 8);
    size_t i = 4;
    if (b[0] & 0x80) {  // extended header
        if (len < 5) return false;
        uint8_t e = b[4];
        i = 5;
        if (e & 0x20) i += 3;  // unicast: destination address
        if (e & 0x10) i += 4;  // signature
        if (i > len) return false;
    }
    const uint8_t* d = b + i;
    size_t n = len - i;
    switch (p.type) {
        case FT_TRACKING: {
            if (n < 11) return false;
            p.lat = s24(d) / 93206.0;
            p.lon = s24(d + 3) / 46603.0;
            uint16_t t = d[6] | (d[7] << 8);
            p.online = t & 0x8000;
            p.aircraft = (t >> 12) & 7;
            p.altM = (float)((t & 0x07FF) * ((t & 0x0800) ? 4 : 1));
            p.speedKmh = unscaled7u(d[8], 0.5f);
            p.climbMs = unscaled7s(d[9], 0.1f);
            p.headingDeg = d[10] * 360.f / 256.f;
            p.hasPos = true;
            break;
        }
        case FT_GROUND: {
            if (n < 7) return false;
            p.lat = s24(d) / 93206.0;
            p.lon = s24(d + 3) / 46603.0;
            p.groundType = d[6] >> 4;
            p.online = d[6] & 1;
            p.hasPos = true;
            break;
        }
        case FT_NAME: {
            for (size_t k = 0; k < n && k < 32; k++) {
                char c = (char)d[k];
                if (c == 0) break;
                p.name += (c >= 32 && c < 127) ? c : '?';  // our font: keep ASCII only
            }
            break;
        }
        case FT_SERVICE: {
            if (n < 1) return false;
            uint8_t f = d[0];
            size_t k = 1;
            if (f & 0x01) k++;  // extended header byte
            // position is present when any data follows
            if (f & 0x7E) {
                if (n < k + 6) return false;
                p.lat = s24(d + k) / 93206.0;
                p.lon = s24(d + k + 3) / 46603.0;
                p.hasPos = true;
                k += 6;
            }
            if (f & 0x40) k++;  // temperature
            if (f & 0x20) {
                if (n < k + 3) return false;
                p.hasWind = true;
                p.windDirDeg = d[k] * 360.f / 256.f;
                p.windKmh = unscaled7u(d[k + 1], 0.2f);
                p.gustKmh = unscaled7u(d[k + 2], 0.2f);
            }
            break;
        }
        default: break;
    }
    return true;
}

std::string fanetAddrStr(uint32_t a) {
    char b[12];
    std::snprintf(b, sizeof b, "%02X%04X", (unsigned)((a >> 16) & 0xFF), (unsigned)(a & 0xFFFF));
    return b;
}

uint32_t fanetTrackingIntervalMs(int n) { return (uint32_t)(std::floor(n / 10.0 + 1) * 5000); }

int clockPosition(float rel) {
    int h = (int)std::lround(wrap360(rel) / 30.0) % 12;
    return h == 0 ? 12 : h;
}

// ---------------------------------------------------------------- traffic table
Traffic& TrafficDb::entry(uint32_t addr, uint32_t nowMs) {
    for (auto& t : list_)
        if (t.addr == addr) return t;
    if (list_.size() >= 40) {  // full: replace the oldest
        auto it = std::min_element(list_.begin(), list_.end(),
                                   [](const Traffic& a, const Traffic& b) { return a.lastMs < b.lastMs; });
        *it = Traffic();
        it->addr = addr;
        it->lastMs = nowMs;
        return *it;
    }
    list_.emplace_back();
    list_.back().addr = addr;
    list_.back().lastMs = nowMs;
    return list_.back();
}

void TrafficDb::onPacket(const FanetPacket& p, uint32_t nowMs, int rssi) {
    if (p.type == FT_NAME) {
        if (p.name.empty()) return;
        for (auto& t : list_)
            if (t.addr == p.src) t.name = p.name;
        // remember the name even before the first position
        if (!find(p.src)) {
            Traffic& t = entry(p.src, nowMs);
            t.name = p.name;
            t.lastMs = 0;  // no position yet -> hidden until tracking arrives
        }
        return;
    }
    if ((p.type != FT_TRACKING && p.type != FT_GROUND) || !p.hasPos) return;
    if (std::fabs(p.lat) > 90 || std::fabs(p.lon) > 180) return;
    Traffic& t = entry(p.src, nowMs);
    t.ground = p.type == FT_GROUND;
    t.lat = p.lat;
    t.lon = p.lon;
    t.lastMs = nowMs;
    t.rssi = rssi;
    if (t.ground) {
        t.groundType = p.groundType;
        t.speedKmh = t.climbMs = 0;
    } else {
        t.aircraft = p.aircraft;
        t.altM = p.altM;
        t.speedKmh = p.speedKmh;
        t.climbMs = p.climbMs;
        t.headingDeg = p.headingDeg;
    }
}

const Traffic* TrafficDb::find(uint32_t addr) const {
    for (auto& t : list_)
        if (t.addr == addr) return &t;
    return nullptr;
}

int TrafficDb::recent(uint32_t nowMs, uint32_t within) const {
    int n = 0;
    for (auto& t : list_)
        if (t.lastMs && nowMs - t.lastMs < within) n++;
    return n;
}

int TrafficDb::maxLevel() const {
    int m = 0;
    for (auto& t : list_) m = std::max(m, t.level);
    return m;
}

void TrafficDb::evaluate(const TrafficOwn& o, uint32_t nowMs) {
    // drop old entries (names without a position are kept for 10 min)
    list_.erase(std::remove_if(list_.begin(), list_.end(),
                               [&](const Traffic& t) {
                                   return t.lastMs ? nowMs - t.lastMs > 120000 : false;
                               }),
                list_.end());
    LocalProj pr;
    if (o.valid) pr.init(o.lat, o.lon);
    float ove, ovn;
    polarToVec(o.trackDeg, o.speedKmh / 3.6f, ove, ovn);
    for (auto& t : list_) {
        t.level = 0;
        if (!t.lastMs) {
            t.ageS = 1e6f;
            continue;
        }
        t.ageS = (nowMs - t.lastMs) / 1000.f;
        if (!o.valid) continue;
        float age = std::min(t.ageS, 10.f);  // dead reckoning limited to 10 s
        float x, y;
        pr.toXY(t.lat, t.lon, x, y);
        float tve = 0, tvn = 0;
        if (!t.ground) polarToVec(t.headingDeg, t.speedKmh / 3.6f, tve, tvn);
        x += tve * age;
        y += tvn * age;
        t.distM = std::hypot(x, y);
        t.brgDeg = (float)wrap360(std::atan2(x, y) * kRad2Deg);
        t.dzM = t.ground ? 0 : (t.altM + t.climbMs * age) - o.altM;
        // closest point of approach (straight lines, constant speeds)
        float rve = tve - ove, rvn = tvn - ovn, v2 = rve * rve + rvn * rvn;
        float tc = v2 > 0.01f ? -(x * rve + y * rvn) / v2 : 0.f;
        tc = std::max(0.f, std::min(60.f, tc));
        t.cpaS = tc;
        t.cpaM = std::hypot(x + rve * tc, y + rvn * tc);
        t.dzCpaM = t.dzM + (t.climbMs - o.climbMs) * std::min(tc, 30.f);
        if (t.ground || t.stale()) continue;
        float adz = std::fabs(t.dzM), adzc = std::fabs(t.dzCpaM);
        if (t.distM < 3000 && adz < 500) t.level = 1;
        if (!o.flying) continue;  // warnings only in the air
        float w = o.warnM, v = o.vertM;
        if ((t.distM < w && adz < v) || (t.cpaM < w && t.cpaS < 45 && adzc < v)) t.level = 2;
        if ((t.distM < w / 2 && adz < v / 2) || (t.cpaM < w / 2 && t.cpaS < 20 && adzc < v / 2)) t.level = 3;
    }
    std::stable_sort(list_.begin(), list_.end(), [](const Traffic& a, const Traffic& b) {
        if (!a.lastMs != !b.lastMs) return a.lastMs != 0;
        if (a.level != b.level) return a.level > b.level;
        return a.distM < b.distM;
    });
}

}  // namespace nav
