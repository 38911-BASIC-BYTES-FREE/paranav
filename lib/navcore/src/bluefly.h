// Parser for the text stream sent by a BlueFlyVario (USB / Bluetooth "transparent UART").
// Supported output modes (BFVDesktop "outputMode"):
//   0  "PRS XXXXX"         raw pressure in Pa, hexadecimal (default)
//   3  "_PRS XXXXX"        FlyNet filtered pressure, hex
//   1  "$LK8EX1,pressure,altitude,vario,temp,battery*CS"   pressure in Pa
//   2  "$LXWP0,..."        barometric (standard) altitude + vario
//   plus "$XCTOD,battery%,tempC" status lines.
// GPS sentences from the vario are deliberately ignored - the internal GNSS is used.
#pragma once
#include <cstdint>

namespace nav {

class BlueFlyParser {
public:
    enum class Kind : uint8_t { None, Pressure, Altitude };
    struct Sample {
        Kind kind = Kind::None;
        float pressureHpa = 0;  // Kind::Pressure
        float altStd = 0;       // Kind::Altitude (ISA standard altitude, m)
    };
    // Feed one character; returns true when a measurement sample is ready (see sample()).
    bool feed(char c);
    const Sample& sample() const { return sample_; }

    int batteryPct() const { return battPct_; }      // -1 unknown
    float batteryVolts() const { return battV_; }     // 0 unknown
    float temperatureC() const { return tempC_; }     // NAN unknown
    const char* format() const { return format_; }    // detected output format
    uint32_t lines() const { return lines_; }
    uint32_t badLines() const { return bad_; }

    bool parseLine(const char* line);  // public for unit tests

private:
    char buf_[96];
    int len_ = 0;
    Sample sample_;
    int battPct_ = -1;
    float battV_ = 0;
    float tempC_ = 0.0f / 0.0f;
    const char* format_ = "?";
    uint32_t lines_ = 0, bad_ = 0;
};

}  // namespace nav
