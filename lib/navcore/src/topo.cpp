#include "topo.h"

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include "filexfer.h"  // crc32Update
#include "yield.h"

namespace nav {

static constexpr double kPi = 3.14159265358979323846;

void topoToPixel(double lat, double lon, int z, double& px, double& py) {
    double scale = (double)kTopoTile * (double)(1u << z);
    lat = std::max(-85.0511, std::min(85.0511, lat));
    double s = std::sin(lat * kPi / 180.0);
    px = (lon + 180.0) / 360.0 * scale;
    py = (0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi)) * scale;
}

void topoToLatLon(double px, double py, int z, double& lat, double& lon) {
    double scale = (double)kTopoTile * (double)(1u << z);
    lon = px / scale * 360.0 - 180.0;
    lat = std::atan(std::sinh(kPi * (1 - 2 * py / scale))) * 180.0 / kPi;
}

double topoMetresPerPixel(double lat, int z) {
    return 40075016.686 * std::cos(lat * kPi / 180.0) / ((double)kTopoTile * (double)(1u << z));
}

static inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool topoDecodeTile(const uint8_t* rec, size_t len, uint8_t* out) {
    if (len < 9) return false;
    uint8_t mode = rec[0];
    uint32_t crc = rd32(rec + 4);
    if (mode > 1) return false;
    size_t i = 8, o = 0;
    while (o < (size_t)kTopoTileBytes && i < len) {
        uint8_t n = rec[i++];
        if (n < 128) {
            size_t k = (size_t)n + 1;
            if (i + k > len || o + k > (size_t)kTopoTileBytes) return false;
            std::memcpy(out + o, rec + i, k);
            i += k;
            o += k;
        } else if (n > 128) {
            size_t k = 257 - (size_t)n;
            if (i >= len || o + k > (size_t)kTopoTileBytes) return false;
            std::memset(out + o, rec[i++], k);
            o += k;
        }
    }
    if (o != (size_t)kTopoTileBytes) return false;
    if (mode == 1) {
        // rows were stored XORed with the row 8 above (periodic fill patterns become zeros)
        uint32_t* w = reinterpret_cast<uint32_t*>(out);  // 8 words per row
        for (int k = 8 * 8; k < kTopoTileBytes / 4; k++) w[k] ^= w[k - 64];
    }
    return crc32Update(0, out, kTopoTileBytes) == crc;
}

bool TopoArea::contains(double lat, double lon) const {
    if (lat > latN || lat < latS || lon < lonW || lon > lonE) return false;
    // inside the circle (+3 % margin, tiles are cut on the circle)
    double dy = (lat - lat0) * 111320.0, dx = (lon - lon0) * 111320.0 * std::cos(lat0 * kPi / 180.0);
    return std::sqrt(dx * dx + dy * dy) <= radiusKm * 1030.0;
}

TopoMap::~TopoMap() { close(); }

void TopoMap::close() {
    if (pak_.f) std::fclose(pak_.f);
    pak_ = Pak();
    for (auto& s : slots_) {
        delete[] s.bits;
        s = Slot();
    }
}

void TopoMap::fail(const std::string& e) {
    errors_++;
    lastError_ = e;
}

static bool parseInfo(const std::string& path, TopoArea& a) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    bool fmt = false, bbox = false;
    while (std::fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == 0) continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        std::string k = p, v = eq + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (k == "format") fmt = std::atoi(v.c_str()) == 1;
        else if (k == "name") a.name = v;
        else if (k == "title") a.title = v;
        else if (k == "osm_date") a.osmDate = v;
        else if (k == "radius_km") a.radiusKm = (float)std::atof(v.c_str());
        else if (k == "center") std::sscanf(v.c_str(), "%lf,%lf", &a.lat0, &a.lon0);
        else if (k == "bbox") bbox = std::sscanf(v.c_str(), "%lf,%lf,%lf,%lf", &a.latN, &a.lonW, &a.latS, &a.lonE) == 4;
        else if (k == "zooms") {
            const char* s = v.c_str();
            while (*s) {
                int z = (int)std::strtol(s, const_cast<char**>(&s), 10);
                if (z >= kTopoMinZoom && z <= kTopoMaxZoom) a.zoomMask |= 1u << z;
                while (*s && (*s < '0' || *s > '9')) s++;
            }
        }
    }
    std::fclose(f);
    if (a.latN < a.latS) std::swap(a.latN, a.latS);
    if (a.lonE < a.lonW) std::swap(a.lonE, a.lonW);
    return fmt && bbox && a.zoomMask && a.radiusKm > 0;
}

int TopoMap::scan() {
    close();
    areas_.clear();
    errors_ = 0;
    lastError_.clear();
    DIR* d = opendir(dir_.c_str());
    if (!d) return 0;
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
        maybeYield(1);
        TopoArea a;
        a.dir = dir_ + "/" + n;
        if (!parseInfo(a.dir + "/info.txt", a)) {
            FILE* t = std::fopen((a.dir + "/info.txt").c_str(), "r");
            if (t) {  // folder with info.txt we cannot understand
                std::fclose(t);
                fail("maps/" + n + ": zły info.txt");
            }
            continue;
        }
        if (a.title.empty()) a.title = a.name.empty() ? n : a.name;
        areas_.push_back(a);
    }
    return (int)areas_.size();
}

int TopoMap::areaFor(double lat, double lon) const {
    int best = -1;
    int bestZ = -1;
    float bestR = 1e9f;
    for (size_t i = 0; i < areas_.size(); i++) {
        const TopoArea& a = areas_[i];
        if (!a.contains(lat, lon)) continue;
        int mz = 31 - __builtin_clz(a.zoomMask);
        if (mz > bestZ || (mz == bestZ && a.radiusKm < bestR)) {
            best = (int)i;
            bestZ = mz;
            bestR = a.radiusKm;
        }
    }
    return best;
}

bool TopoMap::openPak(int area, int z) {
    if (pak_.area == area && pak_.z == z) return !pak_.bad;
    if (pak_.f) std::fclose(pak_.f);
    pak_ = Pak();
    pak_.area = area;
    pak_.z = z;
    if (area < 0 || area >= (int)areas_.size() || !areas_[area].hasZoom(z)) {
        pak_.bad = true;
        return false;
    }
    char fn[16];
    std::snprintf(fn, sizeof fn, "/z%d.pak", z);
    std::string path = areas_[area].dir + fn;
    pak_.f = std::fopen(path.c_str(), "rb");
    uint8_t h[32];
    if (!pak_.f || std::fread(h, 1, 32, pak_.f) != 32 || std::memcmp(h, "PNMP", 4) != 0 || rd16(h + 4) != 1 ||
        h[6] != z) {
        fail(areas_[area].name + fn + ": brak lub zły plik");
        if (pak_.f) std::fclose(pak_.f);
        pak_.f = nullptr;
        pak_.bad = true;
        return false;
    }
    pak_.x0 = rd32(h + 8);
    pak_.y0 = rd32(h + 12);
    pak_.nx = rd16(h + 16);
    pak_.ny = rd16(h + 18);
    pak_.dataOff = rd32(h + 24);
    return true;
}

TopoTile TopoMap::load(int area, int z, int x, int y, uint8_t* out) {
    if (!openPak(area, z)) return TopoTile::NoData;
    if (x < (int)pak_.x0 || y < (int)pak_.y0 || x >= (int)(pak_.x0 + pak_.nx) || y >= (int)(pak_.y0 + pak_.ny))
        return TopoTile::NoData;
    uint32_t idx = (uint32_t)(y - pak_.y0) * pak_.nx + (uint32_t)(x - pak_.x0);
    uint8_t e[8];
    if (std::fseek(pak_.f, (long)(32 + idx * 8), SEEK_SET) != 0 || std::fread(e, 1, 8, pak_.f) != 8) {
        fail("odczyt indeksu mapy");
        return TopoTile::Error;
    }
    uint32_t off = rd32(e), len = rd32(e + 4);
    if (len == 0) return off == 0xFFFFFFFFu ? TopoTile::NoData : TopoTile::White;
    if (len > 2 * kTopoTileBytes || off < pak_.dataOff) {
        fail("uszkodzony indeks mapy");
        return TopoTile::Error;
    }
    rec_.resize(len);
    if (std::fseek(pak_.f, (long)off, SEEK_SET) != 0 || std::fread(rec_.data(), 1, len, pak_.f) != len) {
        fail("odczyt kafelka mapy");
        return TopoTile::Error;
    }
    if (!topoDecodeTile(rec_.data(), len, out)) {
        fail("uszkodzony kafelek mapy (CRC)");
        return TopoTile::Error;
    }
    return TopoTile::Ok;
}

TopoTile TopoMap::tile(int area, int z, int x, int y, const uint8_t** bits) {
    *bits = nullptr;
    clock_++;
    Slot* victim = &slots_[0];
    for (auto& s : slots_) {
        if (s.area == area && s.z == z && s.x == x && s.y == y) {
            s.use = clock_;
            if (s.st == TopoTile::Ok) *bits = s.bits;
            return s.st;
        }
        if (s.use < victim->use) victim = &s;
    }
    if (!victim->bits) victim->bits = new (std::nothrow) uint8_t[kTopoTileBytes];
    if (!victim->bits) return TopoTile::Error;
    victim->area = area;
    victim->z = z;
    victim->x = x;
    victim->y = y;
    victim->use = clock_;
    victim->st = load(area, z, x, y, victim->bits);
    if (victim->st == TopoTile::Ok) *bits = victim->bits;
    return victim->st;
}

}  // namespace nav
