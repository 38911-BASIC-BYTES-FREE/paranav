#include "airspace.h"
#include "i18n.h"
#include "yield.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nav {

// ---------------------------------------------------------------- altitude
float AltLimit::toMsl(float groundM, float qnhHpa) const {
    switch (ref) {
        case MSL: return value;
        case AGL: return (std::isnan(groundM) ? 0.f : groundM) + value;
        case GND: return std::isnan(groundM) ? -1000.f : groundM;
        case UNL: return 99999.f;
        case FL: {
            // pressure altitude -> QNH altitude: ~8.3 m per hPa difference from standard
            float pa = value * 100.f * kFt2M;
            return pa + (qnhHpa - 1013.25f) * 8.3f;
        }
    }
    return value;
}

std::string AltLimit::label() const {
    char b[24];
    switch (ref) {
        case GND: return "GND";
        case UNL: return "UNL";
        case FL: std::snprintf(b, sizeof b, "FL%d", (int)value); return b;
        case AGL: std::snprintf(b, sizeof b, "%dm AGL", (int)std::lround(value)); return b;
        case MSL: std::snprintf(b, sizeof b, "%dm", (int)std::lround(value)); return b;
    }
    return "?";
}

const char* activationLabel(Activation a) {
    switch (a) {
        case Activation::Permanent: return TR("STAŁA", "PERM.");
        case Activation::Active: return TR("AKTYWNA", "ACTIVE");
        case Activation::Inactive: return TR("NIEAKT.", "INACT.");
        default: return TR("AKT.?", "ACT.?");
    }
}

bool Airspace::isActiveAt(uint32_t sec) const {
    uint32_t m = sec / 60;
    for (auto& w : windows) {
        if (w.first <= w.second) {
            if (m >= w.first && m < w.second) return true;
        } else if (m >= w.first || m < w.second) {  // over midnight
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- parsing helpers
static std::string upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}
static std::string trimS(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

AltLimit AirspaceDb::parseAlt(const char* s) {
    AltLimit a;
    std::string u = upper(trimS(s ? s : ""));
    // strip spaces
    std::string c;
    for (char ch : u) if (ch != ' ' && ch != '\t') c += ch;
    if (c.empty() || c == "GND" || c == "SFC" || c == "0" || c == "AGL" || c == "ASFC") { a.ref = AltLimit::GND; return a; }
    if (c.rfind("UNL", 0) == 0) { a.ref = AltLimit::UNL; return a; }
    if (c.rfind("FL", 0) == 0) {
        a.ref = AltLimit::FL;
        a.value = (float)std::atof(c.c_str() + 2);
        return a;
    }
    // number + unit + reference
    size_t i = 0;
    while (i < c.size() && (std::isdigit((unsigned char)c[i]) || c[i] == '.')) i++;
    if (i == 0) {  // e.g. "MSL" alone - treat as 0 MSL
        a.ref = AltLimit::GND;
        return a;
    }
    float v = (float)std::atof(c.substr(0, i).c_str());
    std::string rest = c.substr(i);
    bool metres = false;
    if (rest.rfind("FT", 0) == 0 || rest.rfind("F", 0) == 0) metres = false;
    else if (rest.rfind("M", 0) == 0 && rest.rfind("MSL", 0) != 0) metres = true;
    if (!metres) v *= kFt2M;
    bool agl = rest.find("AGL") != std::string::npos || rest.find("SFC") != std::string::npos ||
               rest.find("GND") != std::string::npos;
    if (agl && v == 0) { a.ref = AltLimit::GND; return a; }
    a.ref = agl ? AltLimit::AGL : AltLimit::MSL;
    a.value = v;
    return a;
}

// Parse one DMS / decimal component like "52:24:30.5", "52:24.5", "522430", "52.408".
// Works on a char range [b, e) without heap allocations (thousands of lines per file on the device).
static bool parseDms(const char* b, const char* e, bool isLon, double& out) {
    while (b < e && std::isspace((unsigned char)*b)) b++;
    while (e > b && std::isspace((unsigned char)e[-1])) e--;
    if (b >= e || e - b > 31) return false;
    char t[32];
    std::memcpy(t, b, e - b);
    t[e - b] = 0;
    if (std::strchr(t, ':')) {
        double parts[3] = {0, 0, 0};
        char* p = t;
        for (int n = 0; n < 3 && p; n++) {
            parts[n] = std::atof(p);
            p = std::strchr(p, ':');
            if (p) p++;
        }
        out = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
        return true;
    }
    const char* dot = std::strchr(t, '.');
    int ipLen = dot ? (int)(dot - t) : (int)std::strlen(t);
    int degDigits = isLon ? 3 : 2;
    char tmp[16];
    auto sub = [&](int from, int len) {
        len = std::max(0, std::min(len, 15));
        std::memcpy(tmp, t + from, len);
        tmp[len] = 0;
        return std::atof(tmp);
    };
    if (ipLen >= degDigits + 4) {  // DDMMSS[.s]
        out = sub(0, ipLen - 4) + sub(ipLen - 4, 2) / 60 + std::atof(t + ipLen - 2) / 3600;
        return true;
    }
    if (ipLen >= degDigits + 2) {  // DDMM[.m]
        out = sub(0, ipLen - 2) + std::atof(t + ipLen - 2) / 60;
        return true;
    }
    out = std::atof(t);
    return true;
}

bool AirspaceDb::parseCoordPair(const char* s, double& lat, double& lon) {
    if (!s) return false;
    char u[96];
    size_t n = 0;
    for (; s[n] && n < sizeof(u) - 1; n++) u[n] = (char)std::toupper((unsigned char)s[n]);
    u[n] = 0;
    const char* ns = std::strpbrk(u, "NS");
    if (!ns) return false;
    const char* ew = std::strpbrk(ns + 1, "EW");
    if (!ew) return false;
    if (!parseDms(u, ns, false, lat)) return false;
    if (!parseDms(ns + 1, ew, true, lon)) return false;
    if (*ns == 'S') lat = -lat;
    if (*ew == 'W') lon = -lon;
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

// ---------------------------------------------------------------- OpenAir
struct AirspaceDb::ParseState {
    Airspace cur;
    bool open = false;
    double cx = 0, cy = 0;  // centre lat, lon (V X=)
    bool clockwise = true;
};

static void addPoint(Airspace& a, double lat, double lon) {
    if (!a.lat.empty() && std::fabs(a.lat.back() - lat) < 1e-7 && std::fabs(a.lon.back() - lon) < 1e-7) return;
    a.lat.push_back((float)lat);
    a.lon.push_back((float)lon);
}

static void addArc(Airspace& a, double clat, double clon, double radiusM, double a0, double a1, bool cw) {
    // a0 -> a1 bearings in degrees, direction cw (+) or ccw (-)
    double span = cw ? wrap360(a1 - a0) : -wrap360(a0 - a1);
    if (std::fabs(span) < 1e-6) span = cw ? 360 : -360;
    int steps = std::max(2, (int)std::ceil(std::fabs(span) / 5.0));
    for (int i = 0; i <= steps; i++) {
        double b = a0 + span * i / steps;
        LatLon p = destination({clat, clon}, b, radiusM);
        addPoint(a, p.lat, p.lon);
    }
}

void AirspaceDb::finishZone(ParseState& st) {
    if (!st.open) return;
    Airspace& a = st.cur;
    if (a.lat.size() > 1 && std::fabs(a.lat.front() - a.lat.back()) < 1e-7 && std::fabs(a.lon.front() - a.lon.back()) < 1e-7) {
        a.lat.pop_back();
        a.lon.pop_back();
    }
    if (a.lat.size() >= 3) {
        for (size_t i = 0; i < a.lat.size(); i++) {
            a.minLat = std::min(a.minLat, a.lat[i]);
            a.maxLat = std::max(a.maxLat, a.lat[i]);
            a.minLon = std::min(a.minLon, a.lon[i]);
            a.maxLon = std::max(a.maxLon, a.lon[i]);
        }
        a.act = isScheduledType(a) ? Activation::Unknown : Activation::Permanent;
        a.lat.shrink_to_fit();
        a.lon.shrink_to_fit();
        zones_.push_back(std::move(a));
    }
    st.cur = Airspace{};
    st.open = false;
    st.clockwise = true;
}

static char* trimInPlace(char* p) {
    while (*p && std::isspace((unsigned char)*p)) p++;
    char* e = p + std::strlen(p);
    while (e > p && std::isspace((unsigned char)e[-1])) *--e = 0;
    return p;
}

static std::string upperStr(const char* p) {
    std::string r(p);
    for (auto& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

void AirspaceDb::handleLine(ParseState& st, char* line) {
    char* hash = std::strchr(line, '*');  // comment
    if (hash) *hash = 0;
    char* l = trimInPlace(line);
    if (!l[0] || !l[1]) return;
    char k0 = (char)std::toupper((unsigned char)l[0]), k1 = (char)std::toupper((unsigned char)l[1]);
    bool vKey = k0 == 'V' && (l[1] == ' ' || l[1] == '\t');
    char* arg = trimInPlace(l + 2);
    if (k0 == 'A' && k1 == 'C') {
        finishZone(st);
        st.open = true;
        st.cur.cls = upperStr(arg);
        return;
    }
    if (!st.open) return;
    if (k0 == 'A') {
        if (k1 == 'N') st.cur.name = arg;
        else if (k1 == 'Y') st.cur.type = upperStr(arg);
        else if (k1 == 'L') st.cur.lower = parseAlt(arg);
        else if (k1 == 'H') st.cur.upper = parseAlt(arg);
        return;
    }
    if (vKey) {
        char* eq = std::strchr(arg, '=');
        if (!eq) return;
        *eq = 0;
        char* var = trimInPlace(arg);
        char* val = trimInPlace(eq + 1);
        char v = (char)std::toupper((unsigned char)var[0]);
        if (v == 'X' && !var[1]) parseCoordPair(val, st.cx, st.cy);
        else if (v == 'D' && !var[1]) st.clockwise = std::strchr(val, '-') == nullptr;
        return;
    }
    if (k0 != 'D') return;
    if (k1 == 'P') {
        double la, lo;
        if (parseCoordPair(arg, la, lo)) addPoint(st.cur, la, lo);
    } else if (k1 == 'C') {
        double r = std::atof(arg) * kNm2M;
        addArc(st.cur, st.cx, st.cy, r, 0, 360, true);
    } else if (k1 == 'A') {  // radius, angleStart, angleEnd
        for (char* p = arg; *p; ++p)
            if (*p == ',') *p = ' ';
        double r = 0, a0 = 0, a1 = 0;
        if (std::sscanf(arg, "%lf %lf %lf", &r, &a0, &a1) == 3)
            addArc(st.cur, st.cx, st.cy, r * kNm2M, a0, a1, st.clockwise);
    } else if (k1 == 'B') {  // coord1 , coord2
        char* comma = std::strchr(arg, ',');
        if (!comma) return;
        *comma = 0;
        double la1, lo1, la2, lo2;
        if (!parseCoordPair(arg, la1, lo1) || !parseCoordPair(comma + 1, la2, lo2)) return;
        LatLon c{st.cx, st.cy};
        double r = 0.5 * (distanceM(c, {la1, lo1}) + distanceM(c, {la2, lo2}));
        addArc(st.cur, st.cx, st.cy, r, bearingDeg(c, {la1, lo1}), bearingDeg(c, {la2, lo2}), st.clockwise);
    }
}

int AirspaceDb::loadOpenAir(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    size_t before = zones_.size();
    ParseState st;
    char line[256];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        char* p = line;
        if (first) {  // skip UTF-8 BOM
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
            first = false;
        }
        handleLine(st, p);
        maybeYield();
    }
    finishZone(st);
    std::fclose(f);
    return (int)(zones_.size() - before);
}

int AirspaceDb::loadOpenAirText(const char* text) {
    size_t before = zones_.size();
    ParseState st;
    const char* p = text;
    char line[256];
    while (*p) {
        size_t n = std::strcspn(p, "\n");
        size_t m = std::min(n, sizeof(line) - 1);
        std::memcpy(line, p, m);
        line[m] = 0;
        handleLine(st, line);
        p += n;
        if (*p == '\n') p++;
    }
    finishZone(st);
    return (int)(zones_.size() - before);
}

size_t AirspaceDb::pointCount() const {
    size_t n = 0;
    for (auto& z : zones_) n += z.lat.size();
    return n;
}

// ---------------------------------------------------------------- activation
bool AirspaceDb::isScheduledType(const Airspace& a) {
    static const char* typePfx[] = {"TRA", "TSA", "EA", "MRT", "D", "R", "Q", "TRA-", "MCTR", "MTMA"};
    for (auto t : typePfx)
        if (a.type == t) return true;
    // OpenAir: R = restricted, Q = danger (class letters A..G are airspace classes, not types)
    if (a.cls == "Q" || a.cls == "R" || a.cls == "TRA" || a.cls == "TSA") return true;
    std::string n = upper(a.name);
    static const char* namePfx[] = {"EPTR", "EPTS", "EPD", "EPR", "EA ", "MRT", "MCTR", "MTMA", "TRA", "TSA"};
    for (auto p : namePfx)
        if (n.rfind(p, 0) == 0) return true;
    return false;
}

static bool nameMatches(const std::string& zoneName, const std::string& token) {
    std::string z = upper(zoneName), t = upper(token);
    if (z.rfind(t, 0) != 0) return false;
    if (z.size() == t.size()) return true;
    char next = z[t.size()];
    return !std::isalnum((unsigned char)next);
}

int AirspaceDb::loadActivations(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    for (auto& z : zones_) z.windows.clear();
    actDate_ = 0;
    int matched = 0;
    char line[200];
    while (std::fgets(line, sizeof(line), f)) {
        maybeYield(100);
        std::string l = trimS(line);
        if (l.empty() || l[0] == '#') continue;
        std::string u = upper(l);
        if (u.rfind("DATE=", 0) == 0) {
            std::string d;
            for (char c : u.substr(5)) if (std::isdigit((unsigned char)c)) d += c;
            actDate_ = (uint32_t)std::atol(d.c_str());
            continue;
        }
        // NAME HHMM-HHMM [HHMM-HHMM ...]
        size_t sp = u.find_first_of(" \t;");
        if (sp == std::string::npos) continue;
        std::string name = u.substr(0, sp);
        std::vector<std::pair<uint16_t, uint16_t>> wins;
        std::string rest = u.substr(sp + 1);
        for (auto& c : rest) if (c == ';' || c == ',') c = ' ';
        const char* p = rest.c_str();
        int h1, m1, h2, m2, used;
        while (std::sscanf(p, " %2d%2d-%2d%2d%n", &h1, &m1, &h2, &m2, &used) == 4) {
            wins.push_back({(uint16_t)(h1 * 60 + m1), (uint16_t)(h2 * 60 + m2)});
            p += used;
        }
        if (wins.empty()) continue;
        for (auto& z : zones_) {
            if (nameMatches(z.name, name)) {
                z.windows.insert(z.windows.end(), wins.begin(), wins.end());
                matched++;
            }
        }
    }
    std::fclose(f);
    return matched;
}

Activation AirspaceDb::activationNow(const Airspace& a, uint32_t utcSec, uint32_t dateYmd) const {
    if (a.act == Activation::Permanent) return Activation::Permanent;
    if (actDate_ == 0 || dateYmd == 0 || actDate_ != dateYmd) return Activation::Unknown;
    return a.isActiveAt(utcSec) ? Activation::Active : Activation::Inactive;
}

// ---------------------------------------------------------------- geometry
bool AirspaceDb::contains(int i, double lat, double lon) const {
    const Airspace& a = zones_[i];
    if (lat < a.minLat || lat > a.maxLat || lon < a.minLon || lon > a.maxLon) return false;
    bool in = false;
    size_t n = a.lat.size();
    for (size_t k = 0, j = n - 1; k < n; j = k++) {
        if (((a.lat[k] > lat) != (a.lat[j] > lat)) &&
            (lon < (a.lon[j] - a.lon[k]) * (lat - a.lat[k]) / (a.lat[j] - a.lat[k]) + a.lon[k]))
            in = !in;
    }
    return in;
}

std::vector<AirspaceHit> AirspaceDb::check(const Query& q, const Settings* filter) const {
    std::vector<AirspaceHit> out;
    LocalProj pr;
    pr.init(q.lat, q.lon);
    float dLat = (float)(q.radiusM / pr.ky), dLon = (float)(q.radiusM / pr.kx);
    float tx, ty;
    polarToVec(q.trackDeg, q.gsMs * q.lookAheadS, tx, ty);
    bool moving = q.gsMs > 2.0f;

    for (size_t zi = 0; zi < zones_.size(); zi++) {
        const Airspace& a = zones_[zi];
        if (q.lat < a.minLat - dLat || q.lat > a.maxLat + dLat || q.lon < a.minLon - dLon || q.lon > a.maxLon + dLon)
            continue;
        if (filter && filter->isIgnoredClass(a.cls)) continue;
        AirspaceHit h;
        h.idx = (int)zi;
        size_t n = a.lat.size();
        float best = 1e12f, bx = 0, by = 0, tmin = 2.f;
        bool in = false;
        float px, py, qx, qy;
        pr.toXY(a.lat[n - 1], a.lon[n - 1], px, py);
        for (size_t k = 0; k < n; k++) {
            pr.toXY(a.lat[k], a.lon[k], qx, qy);
            // inside test (ray along +x from origin)
            if ((qy > 0) != (py > 0)) {
                float xint = px + (0 - py) * (qx - px) / (qy - py);
                if (xint > 0) in = !in;
            }
            // closest point on segment
            float ex = qx - px, ey = qy - py;
            float l2 = ex * ex + ey * ey;
            float t = l2 > 0 ? -(px * ex + py * ey) / l2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float cx = px + t * ex, cy = py + t * ey;
            float d2 = cx * cx + cy * cy;
            if (d2 < best) { best = d2; bx = cx; by = cy; }
            // look-ahead intersection: origin + s*(tx,ty), s in [0,1]
            if (moving) {
                float den = tx * ey - ty * ex;
                if (std::fabs(den) > 1e-6f) {
                    float s = (px * ey - py * ex) / den;
                    float u = (px * ty - py * tx) / den;
                    if (s >= 0 && s <= 1 && u >= 0 && u <= 1 && s < tmin) tmin = s;
                }
            }
            px = qx;
            py = qy;
        }
        h.insideH = in;
        h.distM = in ? 0 : std::sqrt(best);
        h.bearingDeg = vecDir(bx, by);
        if (h.distM > q.radiusM) continue;
        h.timeToEntryS = (!in && tmin <= 1.f) ? tmin * q.lookAheadS : -1;

        h.floorMsl = a.lower.toMsl(q.groundMsl, q.qnhHpa);
        h.ceilMsl = a.upper.toMsl(q.groundMsl, q.qnhHpa);
        if (q.altMsl < h.floorMsl) { h.vpos = VertPos::Below; h.vertMarginM = h.floorMsl - q.altMsl; }
        else if (q.altMsl > h.ceilMsl) { h.vpos = VertPos::Above; h.vertMarginM = q.altMsl - h.ceilMsl; }
        else { h.vpos = VertPos::Inside; h.vertMarginM = -std::min(q.altMsl - h.floorMsl, h.ceilMsl - q.altMsl); }

        Activation act = activationNow(a, q.utcSec, q.dateYmd);
        h.active = act != Activation::Inactive;
        bool vertNear = h.vpos == VertPos::Inside || h.vertMarginM < q.vertWarnM;
        if (!h.active) h.level = 0;
        else if (in && h.vpos == VertPos::Inside) h.level = 2;
        else if (in && vertNear) h.level = 1;
        else if (!in && vertNear && (h.distM < q.horizWarnM || (h.timeToEntryS >= 0 && h.timeToEntryS < q.horizWarnS)))
            h.level = 1;
        out.push_back(h);
    }
    std::sort(out.begin(), out.end(), [](const AirspaceHit& x, const AirspaceHit& y) {
        if (x.level != y.level) return x.level > y.level;
        return x.distM < y.distM;
    });
    return out;
}

}  // namespace nav
