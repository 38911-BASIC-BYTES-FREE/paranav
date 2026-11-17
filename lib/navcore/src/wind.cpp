#include "wind.h"

#include <cmath>

#include "geo.h"

namespace nav {

void WindEstimator::reset() {
    head_ = count_ = 0;
    haveVec_ = false;
    res_ = WindResult{};
}

void WindEstimator::add(uint32_t t, float trackDeg, float gsKmh) {
    if (gsKmh < 8.f) {  // on ground / not meaningful
        if (res_.valid) res_.ageS = t - lastFitT_;
        return;
    }
    float ve, vn;
    polarToVec(trackDeg, gsKmh, ve, vn);
    vx_[head_] = ve;
    vy_[head_] = vn;
    trk_[head_] = trackDeg;
    t_[head_] = t;
    head_ = (head_ + 1) % kMax;
    if (count_ < kMax) count_++;
    if (!tryFit(t) && res_.valid) res_.ageS = t - lastFitT_;
}

bool WindEstimator::fitCircle(const float* x, const float* y, int n, float& cx, float& cy, float& r, float& rms) {
    if (n < 6) return false;
    // Solve min sum (x^2+y^2 + D x + E y + F)^2  -> normal equations
    double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, sz = 0, sxz = 0, syz = 0;
    for (int i = 0; i < n; i++) {
        double xi = x[i], yi = y[i], zi = xi * xi + yi * yi;
        sxx += xi * xi; sxy += xi * yi; syy += yi * yi;
        sx += xi; sy += yi; sz += zi; sxz += xi * zi; syz += yi * zi;
    }
    // | sxx sxy sx | |D|   |-sxz|
    // | sxy syy sy | |E| = |-syz|
    // | sx  sy  n  | |F|   |-sz |
    double a[3][4] = {{sxx, sxy, sx, -sxz}, {sxy, syy, sy, -syz}, {sx, sy, (double)n, -sz}};
    for (int c = 0; c < 3; c++) {
        int piv = c;
        for (int rr = c + 1; rr < 3; rr++)
            if (std::fabs(a[rr][c]) > std::fabs(a[piv][c])) piv = rr;
        if (std::fabs(a[piv][c]) < 1e-9) return false;
        if (piv != c)
            for (int k = 0; k < 4; k++) { double tmp = a[c][k]; a[c][k] = a[piv][k]; a[piv][k] = tmp; }
        for (int rr = 0; rr < 3; rr++) {
            if (rr == c) continue;
            double f = a[rr][c] / a[c][c];
            for (int k = c; k < 4; k++) a[rr][k] -= f * a[c][k];
        }
    }
    double D = a[0][3] / a[0][0], E = a[1][3] / a[1][1], F = a[2][3] / a[2][2];
    cx = (float)(-D / 2);
    cy = (float)(-E / 2);
    double r2 = cx * cx + cy * cy - F;
    if (r2 <= 0) return false;
    r = (float)std::sqrt(r2);
    double e = 0;
    for (int i = 0; i < n; i++) {
        double d = std::hypot(x[i] - cx, y[i] - cy) - r;
        e += d * d;
    }
    rms = (float)std::sqrt(e / n);
    return true;
}

bool WindEstimator::tryFit(uint32_t t) {
    if (count_ < 10) return false;
    if (t - lastFitT_ < 5 && res_.valid) return false;  // fit at most every 5 s
    // walk backwards accumulating heading change; window max 90 s
    float turn = 0;
    int used = 1;
    int idx = (head_ - 1 + kMax) % kMax;
    float prev = trk_[idx];
    for (int k = 1; k < count_; k++) {
        int i = (head_ - 1 - k + 2 * kMax) % kMax;
        if (t - t_[i] > 90) break;
        float d = (float)std::fabs(wrap180(prev - trk_[i]));
        if (d > 60) break;  // GPS glitch / too fast
        turn += d;
        prev = trk_[i];
        used++;
        if (turn >= 360.f) break;
    }
    if (turn < 200.f || used < 10) return false;
    float xs[kMax], ys[kMax];
    for (int k = 0; k < used; k++) {
        int i = (head_ - 1 - k + 2 * kMax) % kMax;
        xs[k] = vx_[i];
        ys[k] = vy_[i];
    }
    float cx, cy, r, rms;
    if (!fitCircle(xs, ys, used, cx, cy, r, rms)) return false;
    float ws = std::hypot(cx, cy);
    if (r < 12.f || r > 90.f || rms > 5.f || ws > 70.f || ws > r * 1.1f) return false;
    // smooth (vector EMA)
    if (!haveVec_) { we_ = cx; wn_ = cy; haveVec_ = true; }
    else { we_ = 0.6f * we_ + 0.4f * cx; wn_ = 0.6f * wn_ + 0.4f * cy; }
    res_.valid = true;
    res_.speedKmh = std::hypot(we_, wn_);
    res_.fromDeg = (float)wrap360(vecDir(we_, wn_) + 180.0);
    res_.airspeedKmh = r;
    res_.rmsKmh = rms;
    res_.ageS = 0;
    lastFitT_ = t;
    return true;
}

}  // namespace nav
