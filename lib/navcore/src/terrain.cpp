#include "terrain.h"
#include "yield.h"

#include <dirent.h>

#include <cctype>
#include <cstring>
#include <vector>

namespace nav {

Terrain::~Terrain() {
    for (auto& t : tiles_)
        if (t.f) std::fclose(t.f);
}

void Terrain::setDir(const std::string& dir) {
    for (auto& t : tiles_) {
        if (t.f) std::fclose(t.f);
        t = Tile{};
    }
    dir_ = dir;
    indexed_ = false;
}

Terrain::Tile* Terrain::getTile(int lat, int lon) {
    clock_++;
    for (auto& t : tiles_)
        if (t.lat == lat && t.lon == lon) {
            t.lastUse = clock_;
            return t.missing ? nullptr : &t;
        }
    // evict LRU
    Tile* v = &tiles_[0];
    for (auto& t : tiles_)
        if (t.lastUse < v->lastUse) v = &t;
    if (v->f) std::fclose(v->f);
    *v = Tile{};
    v->lat = lat;
    v->lon = lon;
    v->lastUse = clock_;
    char name[32];
    std::snprintf(name, sizeof name, "%c%02d%c%03d", lat >= 0 ? 'N' : 'S', std::abs(lat), lon >= 0 ? 'E' : 'W',
                  std::abs(lon));
    if (!indexed_) buildIndex();
    auto it = index_.find(name);
    if (it != index_.end()) v->f = std::fopen(it->second.c_str(), "rb");
    if (!v->f) {
        v->missing = true;
        return nullptr;
    }
    std::fseek(v->f, 0, SEEK_END);
    long sz = std::ftell(v->f);
    if (sz == 3601L * 3601 * 2) v->size = 3601;
    else if (sz == 1201L * 1201 * 2) v->size = 1201;
    else {
        std::fclose(v->f);
        v->f = nullptr;
        v->missing = true;
        return nullptr;
    }
    opened_++;
    return v;
}

int16_t Terrain::sample(Tile* t, int row, int col) {
    long off = ((long)row * t->size + col) * 2;
    uint8_t b[2];
    if (std::fseek(t->f, off, SEEK_SET) != 0 || std::fread(b, 1, 2, t->f) != 2) return -32768;
    return (int16_t)((b[0] << 8) | b[1]);
}

float Terrain::elevation(double lat, double lon) {
    int tlat = (int)std::floor(lat), tlon = (int)std::floor(lon);
    Tile* t = getTile(tlat, tlon);
    if (!t) return NAN;
    double fy = (tlat + 1 - lat) * (t->size - 1);  // row from north
    double fx = (lon - tlon) * (t->size - 1);
    int r = (int)fy, c = (int)fx;
    if (r >= t->size - 1) r = t->size - 2;
    if (c >= t->size - 1) c = t->size - 2;
    double dy = fy - r, dx = fx - c;
    int16_t a = sample(t, r, c), b = sample(t, r, c + 1), cc = sample(t, r + 1, c), d = sample(t, r + 1, c + 1);
    const int16_t VOID = -32768;
    if (a == VOID || b == VOID || cc == VOID || d == VOID) {
        int16_t v[4] = {a, b, cc, d};
        for (auto x : v)
            if (x != VOID) return x;
        return NAN;
    }
    return (float)(a * (1 - dx) * (1 - dy) + b * dx * (1 - dy) + cc * (1 - dx) * dy + d * dx * dy);
}

static std::string upperStr(const char* s) {
    std::string r(s);
    for (auto& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

void Terrain::buildIndex() {
    index_.clear();
    indexed_ = true;
    // breadth: dir, dir/*, dir/*/*
    std::vector<std::pair<std::string, int>> todo{{dir_, 0}};
    while (!todo.empty()) {
        auto [path, depth] = todo.back();
        todo.pop_back();
        DIR* d = opendir(path.c_str());
        if (!d) continue;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            maybeYield(20);
            std::string full = path + "/" + e->d_name;
            std::string up = upperStr(e->d_name);
            if (up.size() == 11 && up.compare(7, 4, ".HGT") == 0) {
                index_.emplace(up.substr(0, 7), full);
            } else if (depth < 2 && up.find('.') == std::string::npos) {
                todo.push_back({full, depth + 1});  // probably a folder
            }
        }
        closedir(d);
    }
}

int Terrain::countTiles() {
    buildIndex();
    return (int)index_.size();
}

}  // namespace nav
