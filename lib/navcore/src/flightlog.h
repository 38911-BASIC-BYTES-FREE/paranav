// Saved flights: list, read and delete the IGC files written by TrackLogger (logs/*.igc, with a
// .gpx of the same name). Used by the flight viewer (Menu > Flights). Plain stdio + dirent, so it
// works on the device (SD card VFS) and in the desktop tests.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nav {

// One flight file in the list (read quickly: header, first and last fix only)
struct FlightFileInfo {
    std::string path, name;  // full path / file name
    int year = 0, month = 0, day = 0;
    int startSec = -1;       // UTC seconds of day of the first fix
    int durationS = 0;
    long bytes = 0;
};

struct FlightPoint {
    int32_t t;       // seconds since the first fix
    float lat, lon;
    int16_t alt;     // m AMSL (barometric, levelled to GPS altitude; GPS altitude without a barometer)
};

struct FlightStats {
    int durationS = 0;
    float distKm = 0;          // track length (5 s steps)
    float maxDistKm = 0;       // farthest from takeoff
    float takeoffAlt = 0, maxAlt = 0, minAlt = 0;
    float maxClimb = 0, maxSink = 0;  // m/s over 10 s
    float maxSpeedKmh = 0;     // over 10 s
    float avgSpeedKmh = 0;     // distance / duration
};

struct FlightLog {
    std::string path;
    int year = 0, month = 0, day = 0;
    int startSec = 0;          // UTC seconds of day of the first fix
    bool baro = false;         // altitude from the pressure sensor
    std::vector<FlightPoint> pts;  // thinned to at most maxPoints
    FlightStats st;
    double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
};

// *.igc files in dir, newest first (by date, then start time, then name). At most maxFiles.
std::vector<FlightFileInfo> listFlights(const std::string& dir, size_t maxFiles = 100);
bool flightInfo(const std::string& path, FlightFileInfo& out);
// Reads the whole file (statistics from every fix, points thinned to maxPoints).
bool loadFlight(const std::string& path, FlightLog& out, size_t maxPoints = 6000, std::string* err = nullptr);
// Removes the .igc and the .gpx of the same name. Returns false if the .igc could not be removed.
bool deleteFlight(const std::string& igcPath);

// IGC B record: time, position, validity, pressure and GNSS altitude. Public for the tests.
struct IgcFix {
    int sec = 0;
    double lat = 0, lon = 0;
    bool valid = false;
    int pressAlt = 0, gpsAlt = 0;
};
bool parseIgcB(const char* line, IgcFix& f);
bool parseIgcDate(const char* line, int& y, int& m, int& d);  // HFDTE line

}  // namespace nav
