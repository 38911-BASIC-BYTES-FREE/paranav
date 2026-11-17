// Geodesy helpers: distances, bearings, local flat projection.
#pragma once
#include <cmath>
#include <cstdint>

namespace nav {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthR = 6371008.8;  // mean Earth radius [m]
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;
constexpr float kNm2M = 1852.0f;
constexpr float kFt2M = 0.3048f;

struct LatLon {
    double lat = 0;
    double lon = 0;
};

inline double wrap360(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}
inline double wrap180(double d) {
    d = wrap360(d);
    return d > 180.0 ? d - 360.0 : d;
}

// Great-circle distance [m] (haversine).
double distanceM(const LatLon& a, const LatLon& b);
// Initial bearing from a to b [deg, 0..360).
double bearingDeg(const LatLon& a, const LatLon& b);
// Point at distance/bearing from a.
LatLon destination(const LatLon& a, double brgDeg, double distM);

// Equirectangular projection around a reference point; metres, x=east, y=north.
// Accurate to <0.5% within ~100 km, which is all we need for airspace checks.
struct LocalProj {
    double lat0 = 0, lon0 = 0, kx = 1, ky = 1;
    void init(double lat, double lon) {
        lat0 = lat;
        lon0 = lon;
        ky = kEarthR * kDeg2Rad;
        kx = ky * std::cos(lat * kDeg2Rad);
    }
    void toXY(double lat, double lon, float& x, float& y) const {
        x = (float)((lon - lon0) * kx);
        y = (float)((lat - lat0) * ky);
    }
    LatLon toLL(float x, float y) const { return {lat0 + y / ky, lon0 + x / kx}; }
};

// Vector helpers (east/north components) for winds and velocities.
inline void polarToVec(float dirDeg, float speed, float& ve, float& vn) {
    ve = speed * (float)std::sin(dirDeg * kDeg2Rad);
    vn = speed * (float)std::cos(dirDeg * kDeg2Rad);
}
inline float vecDir(float ve, float vn) { return (float)wrap360(std::atan2(ve, vn) * kRad2Deg); }

// ISA barometric altitude [m] for pressure p [hPa] relative to reference p0 [hPa].
inline float baroAltitude(float pHpa, float p0Hpa) {
    return 44330.77f * (1.0f - std::pow(pHpa / p0Hpa, 0.190263f));
}
// Inverse: sea-level reference p0 so that pressure p gives altitude h.
inline float qnhFromAltitude(float pHpa, float hM) {
    return pHpa / std::pow(1.0f - hM / 44330.77f, 1.0f / 0.190263f);
}

}  // namespace nav
