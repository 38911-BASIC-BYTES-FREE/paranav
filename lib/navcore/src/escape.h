// Airspace exit ("lead me out") and route-vs-airspace check.
//
// A zone "blocks" a point when it is not an ignored class, is not known to be inactive now, and the
// point is inside it horizontally and between its floor and ceiling - the same rule as the
// "inside airspace" alert (AirspaceHit level 2).
//
// planEscape() compares three ways out and picks the fastest:
//   - horizontal: for every direction (5 deg steps) the distance along that line until no blocking
//     zone is left (zones next to each other are crossed together), + turn time, at max(GS, cruise),
//   - up: climb above the ceiling of every blocking zone above the point (stacked zones too),
//   - down: descend below the floor (impossible when the floor is the ground / too close to it).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "airspace.h"
#include "settings.h"

namespace nav {

struct EscapeQuery {
    double lat = 0, lon = 0;
    float altMsl = 0;
    float groundMsl = NAN;   // terrain below (NAN unknown)
    float qnhHpa = 1013.25f;
    float trackDeg = 0;
    float gsMs = 0;
    float cruiseMs = 11;     // used when slower than this (e.g. hovering into wind)
    float climbMs = 1.5f, sinkMs = 2.0f;
    float turnDegS = 12;     // turn rate for the turn-time estimate
    uint32_t utcSec = 0, dateYmd = 0;
    float maxRangeM = 30000; // horizontal search distance
    float maxAltMsl = NAN;   // highest altitude you may fly, AMSL (NAN = no limit); see maxAltitudeMsl()
};

// Altitude limit (flight level, e.g. 95) as metres AMSL at the current QNH; NAN when fl <= 0.
float maxAltitudeMsl(int fl, float qnhHpa);

struct EscapeOption {
    enum Kind : uint8_t { Horizontal, Up, Down } kind = Horizontal;
    bool valid = false;
    float bearingDeg = 0;  // Horizontal: true course to fly
    float distM = 0;       // Horizontal: to the boundary (+ margin)
    float targetAlt = 0;   // Up / Down: altitude AMSL to reach
    float dAltM = 0;       // Up / Down: altitude change (+ up, - down)
    float timeS = 0;       // estimate
    bool overLimit = false;  // Up: not offered because it would end above the altitude limit
};

struct EscapeResult {
    bool inside = false;             // currently inside at least one blocking zone
    std::vector<int> zones;          // blocking zones at the current position
    EscapeOption horiz, up, down;
    EscapeOption best;               // the fastest valid option (valid = false when none)
};

EscapeResult planEscape(const AirspaceDb& db, const EscapeQuery& q, const Settings* filter);

// ---- route check
struct RouteConflict {
    int zone = -1;
    int leg = 0;              // leg index (0 = first point -> second point)
    float atKm = 0;           // distance from the route start to the first crossing point
    float floorAglM = 0;      // approximate floor above ground at that point
    Activation act = Activation::Permanent;
    bool conflict = false;    // floor below the check height and not known to be inactive
};

// Zones crossed by the route polyline (each zone once, ordered along the route). ground(lat, lon)
// gives the terrain elevation (may return NAN). maxAglM = planned maximum height above ground,
// capped at maxMslM (the altitude limit AMSL, NAN = none).
std::vector<RouteConflict> checkRoute(const AirspaceDb& db, const std::vector<LatLon>& path, const Settings* filter,
                                      uint32_t utcSec, uint32_t dateYmd, float qnhHpa, float maxAglM,
                                      const std::function<float(double, double)>& ground, float maxMslM = NAN);

}  // namespace nav
