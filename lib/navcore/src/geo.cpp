#include "geo.h"

namespace nav {

double distanceM(const LatLon& a, const LatLon& b) {
    double p1 = a.lat * kDeg2Rad, p2 = b.lat * kDeg2Rad;
    double dp = p2 - p1, dl = (b.lon - a.lon) * kDeg2Rad;
    double h = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * kEarthR * std::asin(std::sqrt(h < 1 ? h : 1));
}

double bearingDeg(const LatLon& a, const LatLon& b) {
    double p1 = a.lat * kDeg2Rad, p2 = b.lat * kDeg2Rad, dl = (b.lon - a.lon) * kDeg2Rad;
    double y = std::sin(dl) * std::cos(p2);
    double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return wrap360(std::atan2(y, x) * kRad2Deg);
}

LatLon destination(const LatLon& a, double brgDeg, double distM) {
    double d = distM / kEarthR, th = brgDeg * kDeg2Rad;
    double p1 = a.lat * kDeg2Rad, l1 = a.lon * kDeg2Rad;
    double p2 = std::asin(std::sin(p1) * std::cos(d) + std::cos(p1) * std::sin(d) * std::cos(th));
    double l2 = l1 + std::atan2(std::sin(th) * std::sin(d) * std::cos(p1), std::cos(d) - std::sin(p1) * std::sin(p2));
    return {p2 * kRad2Deg, wrap180(l2 * kRad2Deg)};
}

}  // namespace nav
