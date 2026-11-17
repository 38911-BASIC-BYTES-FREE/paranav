// Alert manager: conditions -> prioritised, acknowledgeable alerts.
#pragma once
#include <cstdint>
#include <string>

namespace nav {

enum AlertId : uint8_t {
    AL_AIRSPACE_IN = 0,  // inside active airspace
    AL_FUEL_RETURN,      // fuel needed to return home exceeds fuel left
    AL_AIRSPACE_NEAR,    // approaching airspace (horizontal / vertical)
    AL_LOW_AGL,
    AL_FUEL_LOW,         // return margin < 10 min
    AL_GPS_LOST,
    AL_BARO_LOST,        // barometer (cable or BlueFly) lost in flight -> altitude from GPS
    AL_BATTERY,
    AL_COUNT
};

struct Alert {
    bool active = false;
    bool acked = false;
    uint32_t since = 0;
    uint32_t ackedAt = 0;
    int key = -1;  // e.g. airspace index; a new key re-triggers the alert
    std::string title, detail;
};

class AlertManager {
public:
    // Update one alert condition. 'now' in seconds. Returns true when it newly fires.
    bool set(AlertId id, bool cond, uint32_t now, const std::string& title = "", const std::string& detail = "",
             int key = -1);
    // Highest-priority active + unacknowledged alert, or -1.
    int top() const;
    // Highest-priority active alert even if acknowledged (for the status bar), or -1.
    int topAny() const;
    void ack(int id, uint32_t now);
    const Alert& get(int id) const { return a_[id]; }
    // Re-arm acknowledged critical alerts after this many seconds.
    uint32_t rearmS = 120;

private:
    Alert a_[AL_COUNT];
};

}  // namespace nav
