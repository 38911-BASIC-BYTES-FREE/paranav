#include "bluefly.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nav {

static bool checksumOk(const char* s) {
    // s starts with '$'; if there is no '*' we accept the line
    const char* star = std::strchr(s, '*');
    if (!star) return true;
    uint8_t cs = 0;
    for (const char* p = s + 1; p < star; ++p) cs ^= (uint8_t)*p;
    return (uint8_t)std::strtol(star + 1, nullptr, 16) == cs;
}

// split comma separated fields in place (max n), returns count
static int splitFields(char* s, char** f, int n) {
    int k = 0;
    f[k++] = s;
    for (char* p = s; *p && k < n; ++p) {
        if (*p == ',') {
            *p = 0;
            f[k++] = p + 1;
        } else if (*p == '*') {
            *p = 0;
            break;
        }
    }
    return k;
}

bool BlueFlyParser::feed(char c) {
    if (c == '\r' || c == '\n') {
        if (len_ == 0) return false;
        buf_[len_] = 0;
        len_ = 0;
        return parseLine(buf_);
    }
    if (len_ < (int)sizeof(buf_) - 1) buf_[len_++] = c;
    else len_ = 0;  // overflow: drop line
    return false;
}

bool BlueFlyParser::parseLine(const char* line) {
    lines_++;
    sample_ = Sample{};
    // "PRS 1873F" / "_PRS 1873F"
    const char* p = line;
    if (*p == '_') p++;
    if (std::strncmp(p, "PRS ", 4) == 0) {
        char* end = nullptr;
        long pa = std::strtol(p + 4, &end, 16);
        if (end == p + 4 || pa < 10000 || pa > 120000) {
            bad_++;
            return false;
        }
        format_ = line[0] == '_' ? "_PRS" : "PRS";
        sample_.kind = Kind::Pressure;
        sample_.pressureHpa = pa / 100.0f;
        return true;
    }
    if (line[0] != '$') return false;  // other text (e.g. "BFV 12" version banner, GPS without $) ignored
    if (!checksumOk(line)) {
        bad_++;
        return false;
    }
    char tmp[96];
    std::strncpy(tmp, line, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    char* f[16];
    int n = splitFields(tmp, f, 16);
    if (std::strcmp(f[0], "$LK8EX1") == 0 && n >= 6) {
        // $LK8EX1,pressure(Pa),altitude(m or 99999),vario(cm/s or 9999),temperature(C),battery(V+1000 or %)
        long pa = std::atol(f[1]);
        if (*f[4]) tempC_ = (float)std::atof(f[4]);
        float b = (float)std::atof(f[5]);
        if (b >= 1000) battPct_ = (int)std::lround(b - 1000);  // LK8000: >1000 means percent
        else if (b > 0) battV_ = b;
        format_ = "LK8EX1";
        if (pa >= 10000 && pa <= 120000 && pa != 999999) {
            sample_.kind = Kind::Pressure;
            sample_.pressureHpa = pa / 100.0f;
            return true;
        }
        long alt = std::atol(f[2]);
        if (*f[2] && alt != 99999) {
            sample_.kind = Kind::Altitude;
            sample_.altStd = (float)alt;
            return true;
        }
        return false;
    }
    if (std::strcmp(f[0], "$LXWP0") == 0 && n >= 4) {
        // $LXWP0,logger,airspeed,baroalt,vario1,...
        if (!*f[3]) return false;
        format_ = "LXWP0";
        sample_.kind = Kind::Altitude;
        sample_.altStd = (float)std::atof(f[3]);
        return true;
    }
    if (std::strcmp(f[0], "$XCTOD") == 0 && n >= 3) {
        if (*f[1]) battPct_ = std::atoi(f[1]);
        if (*f[2]) tempC_ = (float)std::atof(f[2]);
        return false;
    }
    return false;  // $GPRMC etc. from the vario's own GPS: ignored on purpose
}

}  // namespace nav
