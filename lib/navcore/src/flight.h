// Flight computer: combines GPS + baro + terrain + settings into a NavState (1 Hz).
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "geo.h"
#include "nmea.h"
#include "settings.h"
#include "vario.h"
#include "waypoints.h"
#include "wind.h"

namespace nav {

class Terrain;

// Where altitude / vario come from (chosen automatically)
enum class AltSource : uint8_t { None, Wired, BlueFly, Gps };

struct BaroSnapshot {
    bool ok = false;
    AltSource source = AltSource::None;  // Wired or BlueFly when ok
    float pressHpa = 0;
    float tempC = 0;
    float altStd = 0;   // ISA pressure altitude (1013.25) [m], Kalman-filtered
    float vario = 0;    // Kalman vertical speed [m/s]
};

enum class Phase : uint8_t { Ground, Flying };
enum class TargetKind : uint8_t { Home, Waypoint, Route, Landing, Point };

struct TrackPt {
    float lat, lon;
    float vario;
};

struct NavState {
    // time (UTC)
    bool timeValid = false;
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    uint32_t utcSec = 0, dateYmd = 0;
    uint32_t uptimeS = 0;

    // GNSS
    bool fix = false;
    uint8_t sats = 0;
    float hdop = 99;
    double lat = 0, lon = 0;
    float gpsAlt = 0, gsKmh = 0, trackDeg = 0;
    uint32_t fixAgeS = 999;

    // altimeter
    bool baroOk = false;
    AltSource altSource = AltSource::None;
    float pressHpa = 0, tempC = 0, qnh = 1013.25f;
    bool qnhCalibrated = false;
    float altMsl = NAN;     // best altitude AMSL (baro w/ QNH, else GPS)
    float altStd = NAN;     // pressure altitude (FL reference)
    float vario = 0;        // instant (Kalman)
    float vario1s = 0;      // 1 s mean
    float varioAvg = 0;     // 30 s mean
    float groundMsl = NAN;  // terrain under aircraft
    float aglTerrain = NAN; // height above terrain
    // "Have you landed?" question: no movement low above the ground (see FlightComputer)
    bool landingAsk = false;
    uint16_t landingStillS = 0;   // seconds without movement
    uint16_t landingEndInS = 0;   // the flight ends by itself in ... s if the pilot stays still and does not answer
    float aglTakeoff = NAN; // height above takeoff

    // flight
    Phase phase = Phase::Ground;
    uint32_t flightTimeS = 0;
    uint32_t takeoffUtc = 0;
    float maxAlt = NAN, maxClimb = 0, maxSink = 0, distFlownM = 0, maxDistHomeM = 0;
    bool circling = false;
    float thermalGainM = 0, thermalAvg = 0;

    // home
    bool homeSet = false;
    double homeLat = 0, homeLon = 0;
    float homeElev = NAN;
    float homeDistM = 0, homeBrg = 0, homeEteS = -1;
    float homeArrivalAgl = NAN;  // gliding, engine off
    bool homeReachable = false;

    // navigation target
    TargetKind tgtKind = TargetKind::Home;
    bool tgtValid = false;
    std::string tgtName;
    double tgtLat = 0, tgtLon = 0;
    float tgtElev = NAN;
    float tgtDistM = 0, tgtBrg = 0, tgtEteS = -1, tgtReqLD = NAN, tgtArrivalAgl = NAN, tgtFuelArriveL = NAN;
    int routeIdx = 0, routeLen = 0;

    // wind
    WindResult wind;

    // glide
    float ldCurrent = NAN;   // measured ground L/D (only when gliding)
    float ldUsed = 0;        // value used for reach computations

    // fuel
    bool engineOn = true;
    float fuelUsedL = 0, fuelLeftL = 0, enduranceMin = 0;
    float returnNeedL = 0, returnMarginMin = 0;
    bool fuelConfigured = true;
};

class FlightComputer {
public:
    FlightComputer(Settings& s) : set_(s) {}
    void setTerrain(Terrain* t) { terrain_ = t; }
    void setWaypoints(const WaypointDb* w) { wpts_ = w; }
    void setRoute(Route* r) { route_ = r; }

    // Call on each new GPS epoch (1 Hz) with latest baro snapshot. uptime in seconds.
    void update(const GpsFix& g, const BaroSnapshot& b, uint32_t uptimeS);
    // Call when no GPS epoch arrived in the last ~1.5 s (keeps baro values + timers alive)
    void updateNoGps(const BaroSnapshot& b, uint32_t uptimeS);

    const NavState& state() const { return st_; }
    NavState& mutableState() { return st_; }

    // commands
    void setHomeHere();
    void setTargetHome();
    void setTargetWaypoint(int idx);      // index in WaypointDb
    void setTargetNearestLanding();
    void setTargetRoute(int idx);         // start route at idx
    // free point, e.g. another pilot (FANET); position can be updated every second
    void setTargetPoint(const std::string& name, double lat, double lon);
    void updateTargetPoint(double lat, double lon);
    uint32_t targetPointTag = 0;          // caller's id of the point (FANET address)
    void routeNext();
    void routePrev();
    void setEngine(bool on) { st_.engineOn = on; }
    void refuel(float liters);            // set fuel left (resets used)
    void setQnh(float qnh);               // manual
    void setAltitude(float altMsl);       // manual QNH via known altitude
    void resetFlight();

    // Landing detection. 60 s without movement (ground speed < 5 km/h, |vario| < 0.5 m/s):
    //  - at least 50 m above the terrain (SRTM data) -> still flying (hovering into a strong wind), no question,
    //  - otherwise NavState::landingAsk: "LANDED?" with STILL FLYING (default) / LANDED,
    //  - movement (> 8 km/h or |vario| > 1 m/s) closes the question by itself,
    //  - no answer and still no movement for another 4 min -> the flight ends by itself.
    // The landing time (flight duration) is the moment the movement stopped.
    static constexpr uint32_t kLandAskS = 60, kLandAutoEndS = 240, kLandQuietS = 300;
    static constexpr float kLandHighAglM = 50.f;
    void answerStillFlying();  // hide the question, do not ask again for kLandQuietS
    void answerLanded();       // end the flight (at the next update)

    // Reach: still-air glide geometry corrected for wind. Returns arrival height above target
    // ground (target elev) when gliding from current position, NAN if wind makes it impossible.
    float glideArrival(double lat, double lon, float tgtElev) const;
    // Glide range [m] in direction dirDeg from current position (for map ring), terrain = ground under aircraft.
    float glideRangeM(float dirDeg) const;
    // Ground speed [km/h] along bearing for given airspeed with current wind estimate.
    float groundSpeedAlong(float brgDeg, float airKmh) const;

    const std::vector<TrackPt>& track() const { return track_; }
    const std::vector<TrackPt>& recent() const { return recent_; }

    // Flight summary of last completed flight
    struct Summary {
        bool valid = false;
        uint32_t durationS = 0;
        float maxAlt = 0, maxClimb = 0, maxSink = 0, distKm = 0, maxDistHomeKm = 0, fuelUsedL = 0;
    } lastFlight;
    bool takeoffEvent = false, landingEvent = false;  // set for one update cycle

private:
    void common(const BaroSnapshot& b, uint32_t uptimeS);
    void flightLogic(const GpsFix& g);
    void navigation();
    void fuel(float dt);
    void autoQnh(const GpsFix& g);
    void updateTarget();

    Settings& set_;
    Terrain* terrain_ = nullptr;
    const WaypointDb* wpts_ = nullptr;
    Route* route_ = nullptr;
    NavState st_;
    WindEstimator wind_;
    MovingAvg<30> varioAvg30_;
    KalmanVario gpsKv_{0.5f, 4.0f};  // vario from GPS altitude when no barometer is available
    MovingAvg<20> ldVario_, ldGs_;
    MovingAvg<10> turnRate_;
    float lastTrack_ = NAN;
    int fastCount_ = 0, slowCount_ = 0;
    uint32_t stillSince_ = 0, askSince_ = 0, askQuietUntil_ = 0;
    bool pendingLand_ = false;
    void landNow();
    double candLat_ = 0, candLon_ = 0;
    float candAlt_ = NAN;
    uint32_t candUtc_ = 0;
    uint32_t candUptime_ = 0, takeoffUptime_ = 0;
    bool manualHome_ = false;
    uint32_t lastUptime_ = 0;
    int qnhGood_ = 0;
    float qnhAcc_ = 0;
    float circleStartAlt_ = NAN;
    uint32_t circleStartT_ = 0;
    double prevLat_ = 0, prevLon_ = 0;
    bool havePrev_ = false;
    float varioAcc_ = 0;
    int varioN_ = 0;
    std::vector<TrackPt> track_;   // whole flight, every 2 s (decimated when full)
    std::vector<TrackPt> recent_;  // last 90 s @1 Hz
    uint32_t trackTick_ = 0;
    int wptIdx_ = -1;
    std::string ptName_;
    double ptLat_ = 0, ptLon_ = 0;
};

}  // namespace nav
