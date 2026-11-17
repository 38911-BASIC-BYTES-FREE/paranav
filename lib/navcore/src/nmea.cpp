#include "nmea.h"

#include <cstdlib>
#include <cstring>

namespace nav {

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool NmeaParser::feed(char c) {
    if (c == '$') {
        len_ = 0;
        buf_[len_++] = c;
        return false;
    }
    if (len_ == 0) return false;
    if (c == '\r' || c == '\n') {
        buf_[len_] = 0;
        bool r = parseSentence(buf_);
        len_ = 0;
        return r;
    }
    if (len_ < (int)sizeof(buf_) - 1) {
        buf_[len_++] = c;
    } else {
        len_ = 0;  // overflow - drop
    }
    return false;
}

double NmeaParser::parseCoord(const char* f, char hemi) {
    if (!f || !*f) return 0;
    double v = std::atof(f);
    int deg = (int)(v / 100);
    double min = v - deg * 100;
    double d = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W') d = -d;
    return d;
}

bool NmeaParser::parseSentence(const char* s) {
    if (!s || s[0] != '$') return false;
    char tmp[100];
    std::strncpy(tmp, s, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    // checksum
    char* star = std::strchr(tmp, '*');
    if (star) {
        uint8_t cs = 0;
        for (char* p = tmp + 1; p < star; ++p) cs ^= (uint8_t)*p;
        int h = hexVal(star[1]), l = hexVal(star[2]);
        if (h < 0 || l < 0 || ((h << 4) | l) != cs) {
            csErrors_++;
            return false;
        }
        *star = 0;
    }
    sentences_++;
    // split fields
    char* f[24];
    int n = 0;
    char* p = tmp + 1;
    f[n++] = p;
    while (*p && n < 24) {
        if (*p == ',') {
            *p = 0;
            f[n++] = p + 1;
        }
        ++p;
    }
    if (std::strlen(f[0]) < 5) return false;
    const char* type = f[0] + 2;  // skip talker
    if (std::strncmp(type, "RMC", 3) == 0) return handleRmc(f, n);
    if (std::strncmp(type, "GGA", 3) == 0) handleGga(f, n);
    return false;
}

static void parseTime(const char* t, GpsFix& g) {
    if (std::strlen(t) < 6) return;
    g.hour = (t[0] - '0') * 10 + (t[1] - '0');
    g.minute = (t[2] - '0') * 10 + (t[3] - '0');
    g.second = (t[4] - '0') * 10 + (t[5] - '0');
    g.millis = 0;
    if (t[6] == '.') g.millis = (uint16_t)(std::atof(t + 6) * 1000.0 + 0.5);
}

void NmeaParser::handleGga(char** f, int n) {
    if (n < 10) return;
    work_.quality = (uint8_t)std::atoi(f[6]);
    work_.sats = (uint8_t)std::atoi(f[7]);
    work_.hdop = *f[8] ? (float)std::atof(f[8]) : 99.f;
    if (*f[9]) work_.altMsl = (float)std::atof(f[9]);
    ggaValid_ = work_.quality > 0;
    if (ggaValid_) {
        work_.lat = parseCoord(f[2], *f[3]);
        work_.lon = parseCoord(f[4], *f[5]);
    }
}

bool NmeaParser::handleRmc(char** f, int n) {
    if (n < 10) return false;
    parseTime(f[1], work_);
    bool active = f[2][0] == 'A';
    if (active) {
        work_.lat = parseCoord(f[3], *f[4]);
        work_.lon = parseCoord(f[5], *f[6]);
        work_.speedKmh = (float)(std::atof(f[7]) * 1.852);
        work_.courseValid = *f[8] != 0;
        if (work_.courseValid) work_.courseDeg = (float)std::atof(f[8]);
    }
    const char* d = f[9];
    if (std::strlen(d) >= 6) {
        work_.day = (d[0] - '0') * 10 + (d[1] - '0');
        work_.month = (d[2] - '0') * 10 + (d[3] - '0');
        work_.year = 2000 + (d[4] - '0') * 10 + (d[5] - '0');
    }
    work_.valid = active && ggaValid_;
    work_.seq = fix_.seq + 1;
    fix_ = work_;
    return true;
}

}  // namespace nav
