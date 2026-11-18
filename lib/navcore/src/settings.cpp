#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nav {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
bool toBool(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on" || v == "tak"; }
}  // namespace

#define NAV_SETTINGS_FIELDS(X)              \
    X(lang, STR)                            \
    X(pilot, STR)                           \
    X(glider, STR)                          \
    X(gliderId, STR)                        \
    X(fuelStartL, FLT)                      \
    X(fuelFlowLph, FLT)                     \
    X(fuelReserveMin, FLT)                  \
    X(cruiseKmh, FLT)                       \
    X(glideRatio, FLT)                      \
    X(glideSpeedKmh, FLT)                   \
    X(glideMarginM, FLT)                    \
    X(qnhHpa, FLT)                          \
    X(autoQnh, BOOL)                        \
    X(asHorizWarnS, FLT)                    \
    X(asHorizWarnM, FLT)                    \
    X(asVertWarnM, FLT)                     \
    X(asIgnore, STR)                        \
    X(lowAglWarnM, FLT)                     \
    X(battWarnPct, INT)                     \
    X(varioSound, BOOL)                     \
    X(tzOffsetMin, INT)                     \
    X(flip180, BOOL)                        \
    X(mapScaleIdx, INT)                     \
    X(mapNorthUp, BOOL)                     \
    X(topoZoom, INT)                        \
    X(fullRefreshS, INT)                    \
    X(vcomMv, INT)                          \
    X(backlightPct, INT)                    \
    X(backlightOffMin, INT)                 \
    X(blueflyEnabled, BOOL)                 \
    X(phoneEnabled, BOOL)                   \
    X(fanetEnabled, BOOL)                   \
    X(fanetTx, BOOL)                        \
    X(fanetOnline, BOOL)                    \
    X(fanetId, INT)                         \
    X(trafficAlarm, BOOL)                   \
    X(trafficWarnM, FLT)                    \
    X(trafficVertM, FLT)

bool Settings::load(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[200];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
#define STR(f) f = v
#define FLT(f) f = (float)std::atof(v.c_str())
#define INT(f) f = std::atoi(v.c_str())
#define BOOL(f) f = toBool(v)
#define X(name, T) if (k == #name) { T(name); continue; }
        NAV_SETTINGS_FIELDS(X)
#undef X
#undef STR
#undef FLT
#undef INT
#undef BOOL
    }
    std::fclose(f);
    return true;
}

bool Settings::save(const char* path) const {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fprintf(f, "# ParaNav settings - edit on PC or in device menu\n");
#define STR(n) std::fprintf(f, "%s=%s\n", #n, n.c_str())
#define FLT(n) std::fprintf(f, "%s=%.2f\n", #n, (double)n)
#define INT(n) std::fprintf(f, "%s=%d\n", #n, n)
#define BOOL(n) std::fprintf(f, "%s=%d\n", #n, n ? 1 : 0)
#define X(name, T) T(name);
    NAV_SETTINGS_FIELDS(X)
#undef X
#undef STR
#undef FLT
#undef INT
#undef BOOL
    std::fclose(f);
    return true;
}

bool Settings::isIgnoredClass(const std::string& cls) const {
    size_t start = 0;
    while (start <= asIgnore.size()) {
        size_t c = asIgnore.find(',', start);
        if (c == std::string::npos) c = asIgnore.size();
        if (trim(asIgnore.substr(start, c - start)) == cls) return true;
        start = c + 1;
    }
    return false;
}

}  // namespace nav
