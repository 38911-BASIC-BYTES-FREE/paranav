#include "logger.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstring>

namespace nav {

static void igcCoord(char* out, double v, bool lon) {
    char hemi = lon ? (v < 0 ? 'W' : 'E') : (v < 0 ? 'S' : 'N');
    v = std::fabs(v);
    int deg = (int)v;
    int mmm = (int)std::lround((v - deg) * 60000.0);  // minutes * 1000
    if (mmm >= 60000) { deg++; mmm -= 60000; }
    if (lon) std::sprintf(out, "%03d%05d%c", deg, mmm, hemi);
    else std::sprintf(out, "%02d%05d%c", deg, mmm, hemi);
}

static int clampAlt(float a) {
    int v = (int)std::lround(a);
    if (v < -9999) v = -9999;
    if (v > 99999) v = 99999;
    return v;
}

std::string TrackLogger::igcBRecord(const LogPoint& p) {
    char la[16], lo[16], b[96];
    igcCoord(la, p.lat, false);
    igcCoord(lo, p.lon, true);
    int pa = clampAlt(p.pressAlt), ga = clampAlt(p.gpsAlt);
    char pas[16], gas[16];
    if (pa < 0) std::sprintf(pas, "-%04d", -pa); else std::sprintf(pas, "%05d", pa);
    if (ga < 0) std::sprintf(gas, "-%04d", -ga); else std::sprintf(gas, "%05d", ga);
    std::snprintf(b, sizeof b, "B%02d%02d%02d%s%s%c%s%s", p.hour, p.minute, p.second, la, lo, p.valid ? 'A' : 'V',
                  pas, gas);
    return b;
}

std::string TrackLogger::igcFileName(const LogPoint& p, int index) {
    char b[48];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d-XPN-NAV-%02d.igc", p.year, p.month, p.day, index);
    return b;
}

static bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool TrackLogger::start(const char* dir, const LogPoint& first, const std::string& pilot, const std::string& glider,
                        const std::string& gliderId, const char* gpsName, const char* fw) {
    stop();
    mkdir(dir, 0777);
    std::string base;
    for (int i = 1; i < 100; i++) {
        std::string cand = std::string(dir) + "/" + igcFileName(first, i);
        if (!fileExists(cand)) { base = cand; break; }
    }
    if (base.empty()) return false;
    igc_ = std::fopen(base.c_str(), "w");
    if (!igc_) return false;
    igcPath_ = base;
    std::string gpxPath = base.substr(0, base.size() - 4) + ".gpx";
    gpx_ = std::fopen(gpxPath.c_str(), "w");

    FILE* f = igc_;
    std::fprintf(f, "AXPNNAV ParaNav\r\n");
    std::fprintf(f, "HFDTEDATE:%02d%02d%02d,01\r\n", first.day, first.month, first.year % 100);
    std::fprintf(f, "HFFXA035\r\n");
    std::fprintf(f, "HFPLTPILOTINCHARGE:%s\r\n", pilot.c_str());
    std::fprintf(f, "HFCM2CREW2:NIL\r\n");
    std::fprintf(f, "HFGTYGLIDERTYPE:%s\r\n", glider.c_str());
    std::fprintf(f, "HFGIDGLIDERID:%s\r\n", gliderId.c_str());
    std::fprintf(f, "HFDTMGPSDATUM:WGS84\r\n");
    std::fprintf(f, "HFRFWFIRMWAREVERSION:%s\r\n", fw);
    std::fprintf(f, "HFRHWHARDWAREVERSION:LilyGO T5 E-Paper S3 Pro\r\n");
    std::fprintf(f, "HFFTYFRTYPE:ParaNav,T5S3\r\n");
    std::fprintf(f, "HFGPSRECEIVER:%s\r\n", gpsName);
    std::fprintf(f, "HFPRSPRESSALTSENSOR:MEAS,MS5611,12000\r\n");
    std::fprintf(f, "HFALGALTGPS:GEO\r\n");
    std::fprintf(f, "HFALPALTPRESSURE:ISA\r\n");
    std::fflush(f);

    if (gpx_) {
        std::fprintf(gpx_,
                     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<gpx version=\"1.1\" creator=\"ParaNav\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                     "<trk><name>%s</name><trkseg>\n",
                     igcFileName(first, 0).c_str());
    }
    points_ = 0;
    sinceFlush_ = 0;
    return true;
}

void TrackLogger::add(const LogPoint& p) {
    if (!igc_) return;
    std::fprintf(igc_, "%s\r\n", igcBRecord(p).c_str());
    if (gpx_ && p.valid) {
        std::fprintf(gpx_,
                     "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
                     p.lat, p.lon, (double)p.baroAltQnh, p.year, p.month, p.day, p.hour, p.minute, p.second);
    }
    points_++;
    if (++sinceFlush_ >= 10) {
        // fflush + fsync so a power loss loses at most ~10 s of track
        std::fflush(igc_);
        fsync(fileno(igc_));
        if (gpx_) { std::fflush(gpx_); fsync(fileno(gpx_)); }
        sinceFlush_ = 0;
    }
}

void TrackLogger::stop() {
    if (igc_) {
        std::fclose(igc_);
        igc_ = nullptr;
    }
    if (gpx_) {
        std::fprintf(gpx_, "</trkseg></trk>\n</gpx>\n");
        std::fclose(gpx_);
        gpx_ = nullptr;
    }
}

}  // namespace nav
