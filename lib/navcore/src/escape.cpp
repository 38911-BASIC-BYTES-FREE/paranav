#include "escape.h"

#include <algorithm>
#include <cmath>

#include "geo.h"

namespace nav {

namespace {
constexpr float kHorizMarginM = 150;  // fly this far past the boundary
constexpr float kVertMarginM = 30;    // and this far above the ceiling / below the floor
constexpr float kMinAglM = 60;        // never plan a descent closer to the ground than this

bool usable(const AirspaceDb& db, const Airspace& a, const EscapeQuery& q, const Settings* filter) {
    if (filter && filter->isIgnoredClass(a.cls)) return false;
    return db.activationNow(a, q.utcSec, q.dateYmd) != Activation::Inactive;
}
}  // namespace

float maxAltitudeMsl(int fl, float qnhHpa) {
    if (fl <= 0) return NAN;
    AltLimit l;
    l.ref = AltLimit::FL;
    l.value = (float)fl;
    return l.toMsl(NAN, qnhHpa);
}

EscapeResult planEscape(const AirspaceDb& db, const EscapeQuery& q, const Settings* filter) {
    EscapeResult r;
    const auto& zones = db.zones();
    LocalProj pr;
    pr.init(q.lat, q.lon);
    float dLat = (float)(q.maxRangeM / pr.ky), dLon = (float)(q.maxRangeM / pr.kx);

    // zones that matter: near, usable; per zone its vertical limits at the current ground
    struct Z {
        int idx;
        float floor, ceil;
        std::vector<float> x, y;
    };
    std::vector<Z> near;
    for (size_t i = 0; i < zones.size(); i++) {
        const Airspace& a = zones[i];
        if (q.lat < a.minLat - dLat || q.lat > a.maxLat + dLat || q.lon < a.minLon - dLon || q.lon > a.maxLon + dLon) continue;
        if (!usable(db, a, q, filter)) continue;
        Z z;
        z.idx = (int)i;
        z.floor = a.lower.toMsl(q.groundMsl, q.qnhHpa);
        z.ceil = a.upper.toMsl(q.groundMsl, q.qnhHpa);
        near.push_back(std::move(z));
    }
    auto blockingAt = [&](float alt, std::vector<const Z*>& out) {
        out.clear();
        for (const Z& z : near)
            if (alt >= z.floor && alt <= z.ceil && db.contains(z.idx, q.lat, q.lon)) out.push_back(&z);
    };
    std::vector<const Z*> here;
    blockingAt(q.altMsl, here);
    r.inside = !here.empty();
    for (const Z* z : here) r.zones.push_back(z->idx);
    if (!r.inside) return r;

    float speed = std::max(q.gsMs, q.cruiseMs);
    auto turnTime = [&](float course) {
        return (float)std::fabs(wrap180(course - q.trackDeg)) / std::max(1.f, q.turnDegS);
    };

    // ---- horizontal: zones blocking at the current altitude, projected once
    std::vector<Z*> flat;
    for (Z& z : near) {
        if (q.altMsl < z.floor || q.altMsl > z.ceil) continue;
        const Airspace& a = zones[z.idx];
        z.x.resize(a.lat.size());
        z.y.resize(a.lat.size());
        for (size_t k = 0; k < a.lat.size(); k++) pr.toXY(a.lat[k], a.lon[k], z.x[k], z.y[k]);
        flat.push_back(&z);
    }
    std::vector<std::pair<float, float>> iv;
    std::vector<float> ts;
    for (int d = 0; d < 360; d += 5) {
        float ux, uy;
        polarToVec((float)d, 1.f, ux, uy);
        iv.clear();
        for (const Z* z : flat) {
            // crossings of the ray (origin + t*u, t > 0) with every edge; parity gives inside spans
            ts.clear();
            bool in0 = false;
            size_t n = z->x.size();
            for (size_t k = 0, j = n - 1; k < n; j = k++) {
                float px = z->x[j], py = z->y[j], ex = z->x[k] - px, ey = z->y[k] - py;
                // inside test at the origin (ray along +x)
                if ((z->y[k] > 0) != (py > 0)) {
                    float xint = px + (0 - py) * ex / ey;
                    if (xint > 0) in0 = !in0;
                }
                float den = ux * ey - uy * ex;
                if (std::fabs(den) < 1e-9f) continue;
                float t = (px * ey - py * ex) / den;
                float s = (px * uy - py * ux) / den;
                if (t > 0 && s >= 0 && s < 1) ts.push_back(t);
            }
            std::sort(ts.begin(), ts.end());
            size_t k = 0;
            if (in0) {
                iv.push_back({0.f, ts.empty() ? 1e9f : ts[0]});
                k = 1;
            }
            for (; k + 1 < ts.size(); k += 2) iv.push_back({ts[k], ts[k + 1]});
        }
        // walk out through touching / overlapping spans
        float cur = 0;
        for (bool moved = true; moved;) {
            moved = false;
            for (auto& s : iv)
                if (s.first <= cur + 1.f && s.second > cur) {
                    cur = s.second;
                    moved = true;
                }
        }
        if (cur > q.maxRangeM) continue;
        float dist = cur + kHorizMarginM;
        float t = turnTime((float)d) + dist / speed;
        if (!r.horiz.valid || t < r.horiz.timeS) {
            r.horiz.valid = true;
            r.horiz.kind = EscapeOption::Horizontal;
            r.horiz.bearingDeg = (float)d;
            r.horiz.distM = dist;
            r.horiz.timeS = t;
        }
    }

    // ---- up: above the ceiling of everything stacked above us
    {
        float alt = q.altMsl;
        std::vector<const Z*> b;
        bool ok = true;
        for (int i = 0; i < 8; i++) {
            blockingAt(alt, b);
            if (b.empty()) break;
            float top = alt;
            for (const Z* z : b) top = std::max(top, z->ceil);
            if (top > 15000) { ok = false; break; }  // unlimited ceiling
            alt = top + kVertMarginM;
        }
        // never above the altitude limit (e.g. FL95): the target plus the margin must stay below it
        if (ok && alt > q.altMsl && !std::isnan(q.maxAltMsl) && alt > q.maxAltMsl - kVertMarginM) {
            ok = false;
            r.up.overLimit = true;
            r.up.targetAlt = alt;
            r.up.dAltM = alt - q.altMsl;
        }
        if (ok && alt > q.altMsl) {
            r.up.valid = true;
            r.up.kind = EscapeOption::Up;
            r.up.targetAlt = alt;
            r.up.dAltM = alt - q.altMsl;
            r.up.timeS = r.up.dAltM / std::max(0.1f, q.climbMs);
        }
    }
    // ---- down: below the floor, but not into the ground
    {
        float alt = q.altMsl;
        std::vector<const Z*> b;
        bool ok = true;
        float minAlt = (std::isnan(q.groundMsl) ? -1e9f : q.groundMsl + kMinAglM);
        for (int i = 0; i < 8; i++) {
            blockingAt(alt, b);
            if (b.empty()) break;
            float bottom = alt;
            for (const Z* z : b) bottom = std::min(bottom, z->floor);
            alt = bottom - kVertMarginM;
            if (alt < minAlt || bottom < -500) { ok = false; break; }  // floor = ground
        }
        if (ok && alt < q.altMsl) {
            r.down.valid = true;
            r.down.kind = EscapeOption::Down;
            r.down.targetAlt = alt;
            r.down.dAltM = alt - q.altMsl;
            r.down.timeS = -r.down.dAltM / std::max(0.1f, q.sinkMs);
        }
    }
    for (const EscapeOption* o : {&r.horiz, &r.up, &r.down})
        if (o->valid && (!r.best.valid || o->timeS < r.best.timeS)) r.best = *o;
    return r;
}

std::vector<RouteConflict> checkRoute(const AirspaceDb& db, const std::vector<LatLon>& path, const Settings* filter,
                                      uint32_t utcSec, uint32_t dateYmd, float qnhHpa, float maxAglM,
                                      const std::function<float(double, double)>& ground, float maxMslM) {
    std::vector<RouteConflict> out;
    const auto& zones = db.zones();
    std::vector<bool> seen(zones.size(), false);
    float startKm = 0;
    for (size_t leg = 0; leg + 1 < path.size(); leg++) {
        const LatLon &a = path[leg], &b = path[leg + 1];
        double len = distanceM(a, b);
        double lo1 = std::min(a.lat, b.lat), hi1 = std::max(a.lat, b.lat);
        double lo2 = std::min(a.lon, b.lon), hi2 = std::max(a.lon, b.lon);
        int steps = std::max(1, (int)(len / 100.0));  // a sample every ~100 m
        for (size_t zi = 0; zi < zones.size(); zi++) {
            if (seen[zi]) continue;
            const Airspace& z = zones[zi];
            if (z.maxLat < lo1 || z.minLat > hi1 || z.maxLon < lo2 || z.minLon > hi2) continue;
            if (filter && filter->isIgnoredClass(z.cls)) continue;
            for (int s = 0; s <= steps; s++) {
                double f = (double)s / steps;
                double lat = a.lat + (b.lat - a.lat) * f, lon = a.lon + (b.lon - a.lon) * f;
                if (!db.contains((int)zi, lat, lon)) continue;
                RouteConflict c;
                c.zone = (int)zi;
                c.leg = (int)leg;
                c.atKm = startKm + (float)(len * f / 1000.0);
                float g = ground ? ground(lat, lon) : NAN;
                float gm = std::isnan(g) ? 0.f : g;
                c.floorAglM = z.lower.toMsl(g, qnhHpa) - gm;
                if (z.lower.ref == AltLimit::GND) c.floorAglM = 0;
                c.act = db.activationNow(z, utcSec, dateYmd);
                float checkAgl = maxAglM;
                if (!std::isnan(maxMslM)) checkAgl = std::min(checkAgl, maxMslM - gm);  // not above the altitude limit
                c.conflict = c.act != Activation::Inactive && c.floorAglM < checkAgl;
                out.push_back(c);
                seen[zi] = true;
                break;
            }
        }
        startKm += (float)(len / 1000.0);
    }
    std::stable_sort(out.begin(), out.end(), [](const RouteConflict& x, const RouteConflict& y) { return x.atKm < y.atKm; });
    return out;
}

}  // namespace nav
