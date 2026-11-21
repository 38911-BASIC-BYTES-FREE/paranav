// Waypoints from SeeYou .cup files; route = ordered list of waypoints.
#pragma once
#include <string>
#include <vector>

namespace nav {

struct Waypoint {
    std::string name;
    std::string code;
    double lat = 0, lon = 0;
    float elevM = 0;
    int style = 1;  // SeeYou: 1 waypoint, 2..5 airfields/outlanding, ...
    bool isLanding() const { return style >= 2 && style <= 5; }
};

class WaypointDb {
public:
    // Append all waypoints from a .cup file. Returns count or -1.
    int loadCup(const char* path);
    int loadCupText(const char* text);
    // Load every *.cup in a directory (non-recursive).
    int loadDir(const char* dir);
    void clear() { wpts_.clear(); }
    const std::vector<Waypoint>& all() const { return wpts_; }
    // Index of nearest landing field (style 2..5) or -1
    int nearestLanding(double lat, double lon) const;

    static bool parseLine(const std::string& line, Waypoint& w);

private:
    std::vector<Waypoint> wpts_;
};

struct Route {
    std::string name;  // file name
    std::string path;
    std::vector<Waypoint> pts;
    int active = 0;
    int loadCup(const char* path);  // order of the file = order of route
    float lengthM() const;           // sum of the legs
};

}  // namespace nav
