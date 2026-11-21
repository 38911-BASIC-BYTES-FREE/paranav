// Reads the .cup written by the Java planner with the firmware's parser.
#include <cmath>
#include <cstdio>

#include "waypoints.h"

int main() {
    nav::Route r;
    int n = r.loadCup("/tmp/planner_route.cup");
    std::printf("firmware: %d points, length %.1f, elev0 %s, p2 landing %d elev %.0f\n", n, r.lengthM(),
                std::isnan(r.pts[0].elevM) ? "unknown" : "set", r.pts[1].isLanding(), r.pts[1].elevM);
    bool ok = n == 3 && std::isnan(r.pts[0].elevM) && r.pts[1].isLanding() && std::fabs(r.pts[1].elevM - 42) < 0.1 &&
              r.pts[0].name == "Łąka \"Duża\", start";
    std::printf(ok ? "FIRMWARE OK\n" : "FIRMWARE FAIL\n");
    return ok ? 0 : 1;
}
