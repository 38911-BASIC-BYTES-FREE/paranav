// Barometric vario: 2-state Kalman filter (altitude, vertical speed) + averagers.
#pragma once
#include <cmath>
#include <cstdint>

namespace nav {

class KalmanVario {
public:
    // accelNoise: process noise std-dev of vertical acceleration [m/s^2]
    // measNoise: baro altitude noise std-dev [m]
    explicit KalmanVario(float accelNoise = 0.6f, float measNoise = 0.25f)
        : qa_(accelNoise * accelNoise), r_(measNoise * measNoise) {}

    void reset(float alt) {
        h_ = alt;
        v_ = 0;
        p00_ = 10;
        p01_ = p10_ = 0;
        p11_ = 1;
        init_ = true;
    }
    bool initialized() const { return init_; }

    // Move the altitude to a new level but keep the climb rate (a persistent step in the sensor
    // reading, e.g. after a rejected glitch turned out to be real).
    void rebase(float alt) {
        if (!init_) return reset(alt);
        h_ = alt;
        p00_ = 10;
        p01_ = p10_ = 0;
    }

    void update(float z, float dt) {
        if (!init_) {
            reset(z);
            return;
        }
        if (dt <= 0) dt = 0.02f;
        if (dt > 5.0f) {  // long gap (sensor lost / source switched): start over
            reset(z);
            return;
        }
        // predict
        h_ += v_ * dt;
        float dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt2 * dt2;
        float n00 = p00_ + dt * (p10_ + p01_) + dt2 * p11_ + qa_ * dt4 / 4;
        float n01 = p01_ + dt * p11_ + qa_ * dt3 / 2;
        float n10 = p10_ + dt * p11_ + qa_ * dt3 / 2;
        float n11 = p11_ + qa_ * dt2;
        // update
        float y = z - h_;
        float s = n00 + r_;
        float k0 = n00 / s, k1 = n10 / s;
        h_ += k0 * y;
        v_ += k1 * y;
        p00_ = (1 - k0) * n00;
        p01_ = (1 - k0) * n01;
        p10_ = n10 - k1 * n00;
        p11_ = n11 - k1 * n01;
    }
    float altitude() const { return h_; }
    float vario() const { return v_; }

private:
    float qa_, r_;
    float h_ = 0, v_ = 0;
    float p00_ = 10, p01_ = 0, p10_ = 0, p11_ = 1;
    bool init_ = false;
};

// Fixed-window moving average over samples pushed at a fixed rate (e.g. 1 Hz).
template <int N>
class MovingAvg {
public:
    void push(float v) {
        if (count_ == N) sum_ -= buf_[idx_];
        else count_++;
        buf_[idx_] = v;
        sum_ += v;
        idx_ = (idx_ + 1) % N;
    }
    float avg() const { return count_ ? sum_ / count_ : 0; }
    int count() const { return count_; }
    bool full() const { return count_ == N; }
    void clear() { count_ = idx_ = 0; sum_ = 0; }
    // value pushed k samples ago (0 = latest)
    float ago(int k) const {
        if (k >= count_) k = count_ - 1;
        int i = (idx_ - 1 - k + 2 * N) % N;
        return buf_[i];
    }

private:
    float buf_[N] = {};
    float sum_ = 0;
    int count_ = 0, idx_ = 0;
};

}  // namespace nav
