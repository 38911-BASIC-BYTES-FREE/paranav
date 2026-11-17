// Wind estimation from GPS ground-velocity vectors during turns (circle fit).
// When flying at roughly constant airspeed, ground-velocity vectors lie on a
// circle whose centre is the wind vector and whose radius is the airspeed.
#pragma once
#include <cstdint>

namespace nav {

struct WindResult {
    bool valid = false;
    float speedKmh = 0;     // wind speed
    float fromDeg = 0;      // meteorological direction (where the wind comes FROM)
    float airspeedKmh = 0;  // estimated true airspeed during the fit
    float rmsKmh = 0;       // fit residual
    uint32_t ageS = 0;      // seconds since last successful fit
};

class WindEstimator {
public:
    static constexpr int kMax = 120;  // samples (1 Hz -> 2 min window)

    // Add one GPS sample (1 Hz). t in seconds (monotonic).
    void add(uint32_t t, float trackDeg, float gsKmh);
    const WindResult& result() const { return res_; }
    void reset();

    // Kasa least-squares circle fit; returns false if ill-conditioned. Public for tests.
    static bool fitCircle(const float* x, const float* y, int n, float& cx, float& cy, float& r, float& rms);

private:
    bool tryFit(uint32_t t);
    float vx_[kMax], vy_[kMax], trk_[kMax];
    uint32_t t_[kMax];
    int head_ = 0, count_ = 0;
    uint32_t lastFitT_ = 0;
    bool haveVec_ = false;
    float we_ = 0, wn_ = 0;  // smoothed wind vector (blowing towards), km/h
    WindResult res_;
};

}  // namespace nav
