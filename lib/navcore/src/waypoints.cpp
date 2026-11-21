#include "waypoints.h"

#include <cmath>

#include <dirent.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "geo.h"
#include "yield.h"

namespace nav {

static std::vector<std::string> splitCsv(const std::string& l) {
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < l.size(); i++) {
        char c = l[i];
        if (c == '"') {
            if (q && i + 1 < l.size() && l[i + 1] == '"') { cur += '"'; i++; }
            else q = !q;
        } else if (c == ',' && !q) {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r' && c != '\n') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

static bool cupCoord(const std::string& s, bool lon, double& out) {
    // DDMM.mmmN / DDDMM.mmmE
    if (s.size() < 6) return false;
    char h = (char)std::toupper((unsigned char)s.back());
    int dd = lon ? 3 : 2;
    double deg = std::atof(s.substr(0, dd).c_str());
    double mi = std::atof(s.substr(dd, s.size() - dd - 1).c_str());
    out = deg + mi / 60.0;
    if (h == 'S' || h == 'W') out = -out;
    return h == 'N' || h == 'S' || h == 'E' || h == 'W';
}

bool WaypointDb::parseLine(const std::string& line, Waypoint& w) {
    auto f = splitCsv(line);
    if (f.size() < 6) return false;
    if (!cupCoord(f[3], false, w.lat) || !cupCoord(f[4], true, w.lon)) return false;
    w.name = f[0];
    w.code = f[1];
    std::string e = f[5];
    // empty elevation (e.g. a route planned on a map) = unknown: filled from terrain data on the device
    w.elevM = e.find_first_of("0123456789") == std::string::npos ? NAN : (float)std::atof(e.c_str());
    if (e.find("ft") != std::string::npos || e.find("FT") != std::string::npos) w.elevM *= kFt2M;
    w.style = f.size() > 6 ? std::atoi(f[6].c_str()) : 1;
    return true;
}

static int parseCupStream(FILE* f, const char* text, std::vector<Waypoint>& out) {
    int n = 0;
    char buf[300];
    const char* p = text;
    while (true) {
        std::string line;
        if (f) {
            if (!std::fgets(buf, sizeof buf, f)) break;
            line = buf;
        } else {
            if (!*p) break;
            size_t k = std::strcspn(p, "\n");
            line.assign(p, k);
            p += k;
            if (*p) p++;
        }
        if (line.find("-----Related Tasks") != std::string::npos) break;
        maybeYield();
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);  // BOM
        if (line.rfind("name,", 0) == 0 || line.rfind("Title,", 0) == 0 || line.empty()) continue;
        Waypoint w;
        if (WaypointDb::parseLine(line, w)) {
            out.push_back(w);
            n++;
        }
    }
    return n;
}

int WaypointDb::loadCup(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    int n = parseCupStream(f, nullptr, wpts_);
    std::fclose(f);
    return n;
}

int WaypointDb::loadCupText(const char* text) { return parseCupStream(nullptr, text, wpts_); }

int WaypointDb::loadDir(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    while (dirent* e = readdir(d)) {
        size_t l = std::strlen(e->d_name);
        if (l > 4 && (strcasecmp(e->d_name + l - 4, ".cup") == 0)) {
            std::string p = std::string(dir) + "/" + e->d_name;
            int k = loadCup(p.c_str());
            if (k > 0) n += k;
        }
    }
    closedir(d);
    return n;
}

int WaypointDb::nearestLanding(double lat, double lon) const {
    int best = -1;
    double bd = 1e18;
    for (size_t i = 0; i < wpts_.size(); i++) {
        if (!wpts_[i].isLanding()) continue;
        double d = distanceM({lat, lon}, {wpts_[i].lat, wpts_[i].lon});
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

int Route::loadCup(const char* path) {
    pts.clear();
    active = 0;
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    int n = parseCupStream(f, nullptr, pts);
    std::fclose(f);
    const char* s = std::strrchr(path, '/');
    name = s ? s + 1 : path;
    this->path = path;
    return n;
}

float Route::lengthM() const {
    double d = 0;
    for (size_t i = 1; i < pts.size(); i++) d += distanceM({pts[i - 1].lat, pts[i - 1].lon}, {pts[i].lat, pts[i].lon});
    return (float)d;
}

}  // namespace nav
