// User settings stored as key=value in /config.ini on the SD card.
#pragma once
#include <string>

namespace nav {

struct Settings {
    // pilot / logger
    std::string lang = "pl";      // user interface language: pl | en
    std::string pilot = "Pilot";
    std::string glider = "Paramotor";
    std::string gliderId = "";

    // fuel & engine
    float fuelStartL = 10.0f;     // fuel at takeoff [l]
    float fuelFlowLph = 3.5f;     // consumption [l/h]
    float fuelReserveMin = 15.f;  // reserve to keep on landing [min]
    float cruiseKmh = 40.f;       // trim airspeed under power [km/h]

    // glide (engine off) polar
    float glideRatio = 6.0f;      // still-air L/D of wing+motor
    float glideSpeedKmh = 36.f;   // airspeed at best glide
    float glideMarginM = 100.f;   // safety height for reach computations [m]

    // altimeter
    float qnhHpa = 1013.25f;
    bool autoQnh = true;          // calibrate QNH from GPS/terrain at startup

    // alarms
    float asHorizWarnS = 60.f;    // warn this many seconds before entering
    float asHorizWarnM = 500.f;   // ...or when closer than this
    float asVertWarnM = 100.f;    // vertical buffer to floor/ceiling
    std::string asIgnore = "G,W,FIR,UIR,GP";  // airspace classes to ignore (comma list)
    float lowAglWarnM = 0.f;      // 0 = disabled
    int battWarnPct = 20;
    bool varioSound = false;      // no buzzer in v1 hardware

    // display / units / misc
    int tzOffsetMin = 60;         // local time offset to UTC
    bool flip180 = false;         // rotate screen by 180 deg
    int mapScaleIdx = 2;          // index into map scale table
    bool mapNorthUp = false;
    int topoZoom = 13;            // TOPO screen zoom level 10..14
    int fullRefreshS = 60;        // full e-paper refresh period
    int vcomMv = 1560;            // e-paper VCOM (see sticker on panel ribbon)
    int backlightPct = 60;        // front light brightness when switched on
    int backlightOffMin = 0;
    bool blueflyEnabled = true;   // look for a BlueFlyVario over Bluetooth
    // FANET (LoRa 868 MHz): other paragliders / hang gliders around
    bool fanetEnabled = true;     // radio on (receive)
    bool fanetTx = true;          // transmit own position
    bool fanetOnline = true;      // "online tracking" bit: may be shown on the internet via OGN
    int fanetId = 0;              // own 16-bit id (0 = not yet generated)
    bool trafficAlarm = true;     // collision warnings
    float trafficWarnM = 300.f;   // caution distance (alarm at half)
    float trafficVertM = 100.f;   // caution height difference (alarm at half)
    bool phoneEnabled = true;     // accept file uploads from the Android app over Bluetooth      // switch front light off automatically after N minutes (0 = never)

    bool load(const char* path);
    bool save(const char* path) const;
    bool isIgnoredClass(const std::string& cls) const;
};

}  // namespace nav
