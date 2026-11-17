// Airspace database: OpenAir parser, activation schedule, proximity checks.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "geo.h"
#include "settings.h"

namespace nav {

struct AltLimit {
    enum Ref : uint8_t { MSL, AGL, FL, GND, UNL };
    Ref ref = GND;
    float value = 0;  // metres (MSL/AGL) or flight level number (FL)
    // Convert to metres AMSL. groundM = terrain elevation (NAN unknown), qnh in hPa.
    float toMsl(float groundM, float qnhHpa) const;
    std::string label() const;  // short text, e.g. "FL95", "1500m", "GND", "300m AGL"
};

enum class Activation : uint8_t { Permanent, Active, Inactive, Unknown };

struct Airspace {
    std::string name;
    std::string cls;   // AC value (CTR, TMA, R, P, Q, D, C, ...)
    std::string type;  // AY value if present (e.g. TRA, TSA, ATZ, EPR)
    AltLimit lower, upper;
    std::vector<float> lat, lon;  // closed polygon (first != last)
    float minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    Activation act = Activation::Permanent;
    // activation windows from schedule file (minutes of day UTC); empty = none known
    std::vector<std::pair<uint16_t, uint16_t>> windows;
    bool isActiveAt(uint32_t secOfDayUtc) const;
};

enum class VertPos : uint8_t { Below, Inside, Above };

struct AirspaceHit {
    int idx = -1;
    bool insideH = false;      // horizontally inside polygon
    float distM = 0;           // horizontal distance to boundary (0 if inside)
    float bearingDeg = 0;      // to closest boundary point
    float timeToEntryS = -1;   // predicted horizontal entry along current track (-1: none within look-ahead)
    VertPos vpos = VertPos::Inside;
    float floorMsl = 0, ceilMsl = 0;
    float vertMarginM = 0;     // distance to nearest vertical limit (+ = outside, - = inside)
    bool active = true;        // considered active now
    int level = 0;             // 0 info, 1 warning, 2 violation
};

class AirspaceDb {
public:
    // Parse an OpenAir file and append zones. Returns number of zones added, -1 on file error.
    int loadOpenAir(const char* path);
    int loadOpenAirText(const char* text);  // for tests
    // Activation schedule (see README). Returns number of entries matched.
    int loadActivations(const char* path);
    uint32_t activationDate() const { return actDate_; }
    // True for zones whose activity is published daily (AUP/UUP): TRA/TSA/D/R/MRT/EA...
    static bool isScheduledType(const Airspace& a);
    Activation activationNow(const Airspace& a, uint32_t utcSec, uint32_t dateYmd) const;
    void clear() { zones_.clear(); }

    const std::vector<Airspace>& zones() const { return zones_; }
    size_t pointCount() const;

    struct Query {
        double lat, lon;
        float altMsl;        // current altitude AMSL
        float groundMsl;     // terrain elevation (NAN unknown)
        float qnhHpa;
        float trackDeg;
        float gsMs;
        uint32_t utcSec;     // seconds of day, for activation
        float radiusM = 20000;
        float lookAheadS = 120;
        float horizWarnS = 60, horizWarnM = 500, vertWarnM = 100;
        uint32_t dateYmd = 0;  // today's UTC date YYYYMMDD (0 unknown)
    };
    // Returns hits within radius, sorted by severity then distance.
    std::vector<AirspaceHit> check(const Query& q, const Settings* filter = nullptr) const;

    // Inside test for a single point (lat/lon) against zone i.
    bool contains(int i, double lat, double lon) const;

    // Parser helpers (public for unit tests)
    static bool parseCoordPair(const char* s, double& lat, double& lon);
    static AltLimit parseAlt(const char* s);

private:
    struct ParseState;
    void finishZone(ParseState& st);
    void handleLine(ParseState& st, char* line);
    std::vector<Airspace> zones_;
    uint32_t actDate_ = 0;
};

const char* activationLabel(Activation a);

}  // namespace nav
