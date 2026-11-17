// Flight recorder: IGC (primary) + GPX track files.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace nav {

struct LogPoint {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    double lat, lon;
    bool valid;         // 3D fix
    float pressAlt;     // ISA pressure altitude [m] (QNE)
    float gpsAlt;       // GNSS altitude [m]
    float baroAltQnh;   // for GPX <ele>
};

class TrackLogger {
public:
    ~TrackLogger() { stop(); }
    // Open new files in dir. Returns false on error.
    bool start(const char* dir, const LogPoint& first, const std::string& pilot, const std::string& glider,
               const std::string& gliderId, const char* gpsName, const char* fwVersion);
    void add(const LogPoint& p);
    void stop();
    bool active() const { return igc_ != nullptr; }
    const std::string& igcPath() const { return igcPath_; }
    uint32_t points() const { return points_; }

    // Formatting helpers (public for unit tests)
    static std::string igcBRecord(const LogPoint& p);
    static std::string igcFileName(const LogPoint& p, int index);

private:
    FILE* igc_ = nullptr;
    FILE* gpx_ = nullptr;
    std::string igcPath_;
    uint32_t points_ = 0;
    uint32_t sinceFlush_ = 0;
};

}  // namespace nav
