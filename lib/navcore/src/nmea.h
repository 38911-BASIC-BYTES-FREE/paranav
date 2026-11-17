// Minimal NMEA 0183 parser (RMC + GGA, any talker: GP/GN/GL/GA/BD).
#pragma once
#include <cstdint>

namespace nav {

struct GpsFix {
    bool valid = false;      // RMC status A and GGA quality > 0
    uint8_t quality = 0;     // GGA fix quality
    uint8_t sats = 0;
    float hdop = 99.f;
    double lat = 0, lon = 0;
    float altMsl = 0;        // GGA altitude above geoid [m]
    float speedKmh = 0;
    float courseDeg = 0;
    bool courseValid = false;
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    uint16_t millis = 0;
    uint32_t seq = 0;        // incremented for every emitted epoch
    // seconds since midnight UTC
    uint32_t secOfDay() const { return hour * 3600u + minute * 60u + second; }
};

class NmeaParser {
public:
    // Feed one character. Returns true when a new epoch (RMC) has been completed.
    bool feed(char c);
    const GpsFix& fix() const { return fix_; }
    uint32_t sentences() const { return sentences_; }
    uint32_t checksumErrors() const { return csErrors_; }

    // Exposed for tests: parse one full sentence (without CR/LF). Returns true on new epoch.
    bool parseSentence(const char* s);

    // Helpers (public for unit tests)
    static double parseCoord(const char* field, char hemi);

private:
    bool handleRmc(char** f, int n);
    void handleGga(char** f, int n);

    char buf_[100];
    int len_ = 0;
    GpsFix fix_;
    GpsFix work_;
    uint32_t sentences_ = 0, csErrors_ = 0;
    bool ggaValid_ = false;
};

}  // namespace nav
