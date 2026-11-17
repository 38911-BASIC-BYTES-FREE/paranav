#include "ms5611.h"

#include "config.h"

static constexpr uint8_t CMD_RESET = 0x1E;
static constexpr uint8_t CMD_D1_4096 = 0x48;
static constexpr uint8_t CMD_D2_4096 = 0x58;
static constexpr uint8_t CMD_ADC = 0x00;
static constexpr uint8_t CMD_PROM = 0xA0;
static constexpr uint32_t CONV_US = 9500;  // OSR 4096: max 9.04 ms

bool MS5611::cmd(uint8_t c) {
    w_->beginTransmission(addr_);
    w_->write(c);
    return w_->endTransmission() == 0;
}

bool MS5611::readAdc(uint32_t& v) {
    if (!cmd(CMD_ADC)) return false;
    if (w_->requestFrom((int)addr_, 3) != 3) return false;
    v = ((uint32_t)w_->read() << 16) | ((uint32_t)w_->read() << 8) | w_->read();
    return v != 0;
}

bool MS5611::readProm() {
    for (int i = 0; i < 8; i++) {
        if (!cmd(CMD_PROM + i * 2)) return false;
        if (w_->requestFrom((int)addr_, 2) != 2) return false;
        c_[i] = ((uint16_t)w_->read() << 8) | w_->read();
    }
    // CRC4 (AN520)
    uint16_t prom[8];
    memcpy(prom, c_, sizeof prom);
    uint16_t crcRead = prom[7] & 0x000F;
    prom[7] &= 0xFF00;
    uint16_t rem = 0;
    for (int cnt = 0; cnt < 16; cnt++) {
        if (cnt % 2 == 1) rem ^= (uint16_t)(prom[cnt >> 1] & 0x00FF);
        else rem ^= (uint16_t)(prom[cnt >> 1] >> 8);
        for (int b = 8; b > 0; b--) rem = (rem & 0x8000) ? (rem << 1) ^ 0x3000 : (rem << 1);
    }
    rem = (rem >> 12) & 0x000F;
    return rem == crcRead && c_[1] != 0;
}

bool MS5611::begin(TwoWire& w) {
    w_ = &w;
    const uint8_t addrs[2] = {MS5611_ADDR_A, MS5611_ADDR_B};
    for (uint8_t a : addrs) {
        addr_ = a;
        if (!cmd(CMD_RESET)) continue;
        delay(5);
        if (readProm()) {
            ok_ = true;
            st_ = IDLE;
            consecutiveErr_ = 0;
            haveD2_ = false;
            return true;
        }
    }
    addr_ = 0;
    ok_ = false;
    return false;
}

void MS5611::compute() {
    int64_t dT = (int64_t)d2_ - ((int64_t)c_[5] << 8);
    int64_t temp = 2000 + ((dT * c_[6]) >> 23);
    int64_t off = ((int64_t)c_[2] << 16) + (((int64_t)c_[4] * dT) >> 7);
    int64_t sens = ((int64_t)c_[1] << 15) + (((int64_t)c_[3] * dT) >> 8);
    if (temp < 2000) {  // second order compensation
        int64_t t2 = (dT * dT) >> 31;
        int64_t d = temp - 2000;
        int64_t off2 = 5 * d * d / 2;
        int64_t sens2 = 5 * d * d / 4;
        if (temp < -1500) {
            int64_t e = temp + 1500;
            off2 += 7 * e * e;
            sens2 += 11 * e * e / 2;
        }
        temp -= t2;
        off -= off2;
        sens -= sens2;
    }
    int64_t p = ((((int64_t)d1_ * sens) >> 21) - off) >> 15;
    t_ = temp / 100.0f;
    p_ = p / 100.0f;
}

void MS5611::fail() {
    errors_++;
    // sensor unplugged? after 10 failed transfers in a row report it as missing (main code re-probes it)
    if (++consecutiveErr_ >= 10) {
        ok_ = false;
        st_ = IDLE;
    }
}

bool MS5611::poll() {
    if (!ok_) return false;
    uint32_t now = micros();
    switch (st_) {
        case IDLE:
            // temperature every 20th cycle (and first)
            if (!haveD2_ || ++cycle_ >= 20) {
                cycle_ = 0;
                if (!cmd(CMD_D2_4096)) { fail(); return false; }
                st_ = WAIT_D2;
            } else {
                if (!cmd(CMD_D1_4096)) { fail(); return false; }
                st_ = WAIT_D1;
            }
            t0_ = now;
            return false;
        case WAIT_D2:
            if (now - t0_ < CONV_US) return false;
            st_ = IDLE;
            if (!readAdc(d2_)) { fail(); return false; }
            haveD2_ = true;
            return false;
        case WAIT_D1: {
            if (now - t0_ < CONV_US) return false;
            st_ = IDLE;
            if (!readAdc(d1_)) { fail(); return false; }
            compute();
            consecutiveErr_ = 0;
            return p_ > 100 && p_ < 1200;
        }
    }
    return false;
}
