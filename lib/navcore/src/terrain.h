// Digital elevation model from SRTM .hgt tiles (SRTM3 1201x1201 or SRTM1 3601x3601).
// Files: <dir>/N52E021.hgt  (big-endian int16, rows from north to south)
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace nav {

class Terrain {
public:
    ~Terrain();
    // Tiles may sit directly in dir or one/two folder levels below (e.g. /terrain/M34/N52E021.hgt
    // exactly as unpacked from a viewfinderpanoramas.org zip). File name case does not matter.
    void setDir(const std::string& dir);
    // Elevation [m AMSL] with bilinear interpolation; NAN if no tile.
    float elevation(double lat, double lon);
    int tilesOpened() const { return opened_; }
    int countTiles();  // number of .hgt files found

private:
    struct Tile {
        int lat = 999, lon = 999;
        FILE* f = nullptr;
        int size = 0;  // samples per side
        uint32_t lastUse = 0;
        bool missing = false;
    };
    Tile* getTile(int lat, int lon);
    int16_t sample(Tile* t, int row, int col);
    static constexpr int kSlots = 4;
    Tile tiles_[kSlots];
    void buildIndex();
    std::string dir_ = "/sd/terrain";
    std::map<std::string, std::string> index_;  // "N52E021" -> full path
    bool indexed_ = false;
    uint32_t clock_ = 0;
    int opened_ = 0;
};

}  // namespace nav
