// MS5611 barometric pressure sensor (I2C) with 2nd-order temperature compensation.
#pragma once
#include <Arduino.h>
#include <Wire.h>

class MS5611 {
public:
    bool begin(TwoWire& w);
    uint8_t address() const { return addr_; }
    // Non-blocking state machine; call often. Returns true when a new pressure value is ready.
    bool poll();
    float pressureHpa() const { return p_; }
    float temperatureC() const { return t_; }
    bool ok() const { return ok_; }
    uint32_t errors() const { return errors_; }

private:
    bool cmd(uint8_t c);
    bool readAdc(uint32_t& v);
    bool readProm();
    void compute();
    TwoWire* w_ = nullptr;
    uint8_t addr_ = 0;
    uint16_t c_[8] = {};
    uint32_t d1_ = 0, d2_ = 0;
    float p_ = 0, t_ = 0;
    bool ok_ = false;
    enum State { IDLE, WAIT_D1, WAIT_D2 } st_ = IDLE;
    uint32_t t0_ = 0;
    int cycle_ = 0;
    bool haveD2_ = false;
    uint32_t errors_ = 0;
    int consecutiveErr_ = 0;
    void fail();
};
