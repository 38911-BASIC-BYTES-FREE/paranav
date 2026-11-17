// Topographic background map: 1-bit 256x256 tiles (Web Mercator) packed per zoom level,
// produced on a PC by tools/topo/make_tiles.py. SD layout:
//   /sd/maps/<area>/info.txt
//   /sd/maps/<area>/z10.pak ... z14.pak
// See make_tiles.py for the .pak format.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace nav {

constexpr int kTopoTile = 256;
constexpr int kTopoTileBytes = kTopoTile * kTopoTile / 8;  // 8192
constexpr int kTopoMinZoom = 10, kTopoMaxZoom = 14;

// Web Mercator: lat/lon -> global pixel coords at zoom z (256 px tiles) and back
void topoToPixel(double lat, double lon, int z, double& px, double& py);
void topoToLatLon(double px, double py, int z, double& lat, double& lon);
// metres per pixel at latitude lat, zoom z
double topoMetresPerPixel(double lat, int z);

// PackBits + optional XOR-8 row filter. rec = whole tile record (8-byte header + data).
// out: 8192 bytes, 32 bytes per row, MSB = leftmost pixel, 1 = black. Returns false on bad data/CRC.
bool topoDecodeTile(const uint8_t* rec, size_t len, uint8_t* out);

struct TopoArea {
    std::string name, title, dir, osmDate;
    double lat0 = 0, lon0 = 0;            // centre
    double latN = 0, lonW = 0, latS = 0, lonE = 0;  // bounding box
    float radiusKm = 0;
    uint32_t zoomMask = 0;                // bit z set -> zN.pak listed in info.txt
    bool hasZoom(int z) const { return z >= 0 && z < 32 && (zoomMask >> z) & 1; }
    bool contains(double lat, double lon) const;
};

enum class TopoTile : uint8_t { Ok, White, NoData, Error };

class TopoMap {
public:
    ~TopoMap();
    void setDir(const std::string& dir) { dir_ = dir; }
    // Scans <dir>/*/info.txt. Closes open files and drops the cache. Returns number of areas.
    int scan();
    void close();  // release files + cache (before the SD card is handed over to USB)
    const std::vector<TopoArea>& areas() const { return areas_; }
    // Best area for a position (contains it, most detailed zoom, smallest radius) or -1.
    int areaFor(double lat, double lon) const;
    // Tile lookup. On Ok *bits points to a cached 8192-byte bitmap, valid until kSlots other
    // tiles have been requested.
    TopoTile tile(int area, int z, int x, int y, const uint8_t** bits);
    int errors() const { return errors_; }
    const std::string& lastError() const { return lastError_; }
    static constexpr int kSlots = 28;  // a full screen needs at most 5x4 = 20 tiles

private:
    struct Pak {
        int area = -1, z = -1;
        FILE* f = nullptr;
        uint32_t x0 = 0, y0 = 0, dataOff = 0;
        uint16_t nx = 0, ny = 0;
        bool bad = false;
    };
    struct Slot {
        int area = -1, z = -1, x = 0, y = 0;
        uint32_t use = 0;
        TopoTile st = TopoTile::NoData;
        uint8_t* bits = nullptr;
    };
    bool openPak(int area, int z);
    TopoTile load(int area, int z, int x, int y, uint8_t* out);
    void fail(const std::string& e);
    std::string dir_ = "/sd/maps";
    std::vector<TopoArea> areas_;
    Pak pak_;
    Slot slots_[kSlots];
    std::vector<uint8_t> rec_;
    uint32_t clock_ = 0;
    int errors_ = 0;
    std::string lastError_;
};

// View state of the TOPO screen (kept between frames: the map is only re-centred when the
// pilot has moved ~1/5 of the screen, so e-paper does not repaint the whole map every second).
struct TopoView {
    int z = 13;
    bool valid = false;
    double cx = 0, cy = 0;  // global pixel coords of the view centre at zoom z
    int area = -1;
};

}  // namespace nav
