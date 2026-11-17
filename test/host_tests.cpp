// Host unit tests for navcore. Run: tools/run_tests.sh
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "airspace.h"
#include "alerts.h"
#include "bluefly.h"
#include "filexfer.h"
#include "flight.h"
#include "geo.h"
#include "logger.h"
#include "nmea.h"
#include "settings.h"
#include "terrain.h"
#include "topo.h"
#include "vario.h"
#include "waypoints.h"
#include "wind.h"

using namespace nav;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (tol))

static std::string withCs(const char* body) {
    uint8_t cs = 0;
    for (const char* p = body; *p; ++p) cs ^= (uint8_t)*p;
    char b[128];
    std::snprintf(b, sizeof b, "$%s*%02X", body, cs);
    return b;
}

static void testNmea() {
    NmeaParser p;
    CHECK(!p.parseSentence(withCs("GNGGA,101530.00,5212.3456,N,02051.0000,E,1,09,0.9,123.4,M,33.0,M,,").c_str()));
    CHECK(p.parseSentence(withCs("GNRMC,101530.00,A,5212.3456,N,02051.0000,E,24.3,045.5,161126,,,A").c_str()));
    const GpsFix& f = p.fix();
    CHECK(f.valid);
    NEAR(f.lat, 52 + 12.3456 / 60, 1e-7);
    NEAR(f.lon, 20 + 51.0 / 60, 1e-7);
    NEAR(f.altMsl, 123.4, 1e-3);
    NEAR(f.speedKmh, 24.3 * 1.852, 1e-3);
    NEAR(f.courseDeg, 45.5, 1e-3);
    CHECK(f.sats == 9 && f.year == 2026 && f.month == 11 && f.day == 16 && f.hour == 10 && f.minute == 15);
    // bad checksum rejected
    CHECK(!p.parseSentence("$GNRMC,101531.00,A,5212.3456,N,02051.0000,E,24.3,045.5,161126,,,A*00"));
    CHECK(p.checksumErrors() == 1);
    // feed char by char, southern/western hemisphere
    NmeaParser q;
    std::string s1 = withCs("GPGGA,000001,3330.000,S,07030.000,W,1,05,1.5,10,M,,M,,") + "\r\n";
    std::string s2 = withCs("GPRMC,000001,A,3330.000,S,07030.000,W,0.0,,010125,,") + "\r\n";
    bool ep = false;
    for (char c : s1 + s2) ep |= q.feed(c);
    CHECK(ep);
    NEAR(q.fix().lat, -33.5, 1e-9);
    NEAR(q.fix().lon, -70.5, 1e-9);
    // void fix
    NmeaParser r;
    r.parseSentence(withCs("GPRMC,000001,V,,,,,,,010125,,").c_str());
    CHECK(!r.fix().valid);
}

static void testGeo() {
    LatLon a{52.0, 21.0}, b{52.0, 21.1};
    NEAR(distanceM(a, b), 6850, 30);  // ~6.85 km
    NEAR(bearingDeg(a, b), 90, 0.1);
    LatLon c = destination(a, 45, 10000);
    NEAR(distanceM(a, c), 10000, 1);
    NEAR(bearingDeg(a, c), 45, 0.05);
    NEAR(baroAltitude(1013.25f, 1013.25f), 0, 0.01);
    NEAR(baroAltitude(899.0f, 1013.25f), 1000, 5);
    NEAR(qnhFromAltitude(899.0f, baroAltitude(899.0f, 1020.f)), 1020.f, 0.01);
}

static void testKalman() {
    KalmanVario kv;
    float h = 300;
    unsigned seed = 1;
    for (int i = 0; i < 50 * 20; i++) {
        h += 2.0f * 0.02f;
        seed = seed * 1103515245u + 12345u;
        float noise = ((seed >> 16) % 1000 / 1000.f - 0.5f) * 0.6f;
        kv.update(h + noise, 0.02f);
    }
    NEAR(kv.vario(), 2.0, 0.2);
    NEAR(kv.altitude(), h, 0.5);
}

static void testWind() {
    WindEstimator w;
    float air = 38, wFrom = 270, wSpd = 20;
    float wte, wtn;
    polarToVec(wFrom + 180, wSpd, wte, wtn);
    float hdg = 0;
    for (uint32_t t = 0; t < 40; t++) {
        float ae, an;
        polarToVec(hdg, air, ae, an);
        float ve = ae + wte, vn = an + wtn;
        w.add(t, vecDir(ve, vn), std::hypot(ve, vn));
        hdg += 12;  // 30 s full circle
    }
    CHECK(w.result().valid);
    NEAR(w.result().speedKmh, 20, 1.0);
    NEAR(wrap180(w.result().fromDeg - 270), 0, 3);
    NEAR(w.result().airspeedKmh, 38, 1.0);
    // straight flight never produces an estimate
    WindEstimator s;
    for (uint32_t t = 0; t < 100; t++) s.add(t, 90, 50);
    CHECK(!s.result().valid);
}

static const char* kOpenAir = R"(* test
AC D
AN CTR TEST
AL GND
AH 2500ft AMSL
V X=52:00:00 N 021:00:00 E
DC 2

AC R
AN EPTR77 TEST
AY TRA
AL 1000ft AGL
AH FL65
DP 52:10:00 N 021:00:00 E
DP 52:10:00 N 021:10:00 E
DP 52:05:00 N 021:10:00 E
DP 52:05:00 N 021:00:00 E

AC P
AN EPP77
AL 0
AH 3000 ft MSL
V X=52.3N 20.5E
V D=-
DA 1, 90, 270
DP 52:18:00 N 020:30:00 E
)";

static void testAirspace() {
    // altitude parsing
    AltLimit a = AirspaceDb::parseAlt("FL65");
    CHECK(a.ref == AltLimit::FL && a.value == 65);
    a = AirspaceDb::parseAlt("2500ft AMSL");
    CHECK(a.ref == AltLimit::MSL);
    NEAR(a.value, 762, 0.5);
    a = AirspaceDb::parseAlt("1000 ft AGL");
    CHECK(a.ref == AltLimit::AGL);
    NEAR(a.value, 304.8, 0.1);
    a = AirspaceDb::parseAlt("1500m");
    CHECK(a.ref == AltLimit::MSL && a.value == 1500);
    a = AirspaceDb::parseAlt("1500 M AGL");
    CHECK(a.ref == AltLimit::AGL && a.value == 1500);
    CHECK(AirspaceDb::parseAlt("SFC").ref == AltLimit::GND);
    CHECK(AirspaceDb::parseAlt("0").ref == AltLimit::GND);
    CHECK(AirspaceDb::parseAlt("UNLTD").ref == AltLimit::UNL);
    CHECK(AirspaceDb::parseAlt("3000").ref == AltLimit::MSL);  // default feet MSL
    NEAR(AirspaceDb::parseAlt("3000").value, 914.4, 0.1);
    // FL to MSL conversion with QNH
    AltLimit fl{AltLimit::FL, 65};
    NEAR(fl.toMsl(NAN, 1013.25f), 1981, 1);
    NEAR(fl.toMsl(NAN, 1023.25f), 1981 + 83, 1);

    // coordinates
    double la, lo;
    CHECK(AirspaceDb::parseCoordPair("52:10:30 N 021:05:15 E", la, lo));
    NEAR(la, 52.175, 1e-6);
    NEAR(lo, 21.0875, 1e-6);
    CHECK(AirspaceDb::parseCoordPair("52:10.5N 021:05.25E", la, lo));
    NEAR(la, 52.175, 1e-6);
    CHECK(AirspaceDb::parseCoordPair("521030N 0210515E", la, lo));
    NEAR(lo, 21.0875, 1e-6);
    CHECK(AirspaceDb::parseCoordPair("33:30:00 S 070:30:00 W", la, lo));
    NEAR(la, -33.5, 1e-9);
    NEAR(lo, -70.5, 1e-9);

    AirspaceDb db;
    CHECK(db.loadOpenAirText(kOpenAir) == 3);
    const auto& z = db.zones();
    CHECK(z[0].name == "CTR TEST" && z[0].cls == "D");
    // circle radius 2 NM
    NEAR(distanceM({52, 21}, {z[0].lat[0], z[0].lon[0]}), 2 * 1852, 5);
    CHECK(z[0].act == Activation::Permanent);
    CHECK(z[1].act == Activation::Unknown);  // TRA -> scheduled
    CHECK(z[2].act == Activation::Permanent);
    CHECK(db.contains(0, 52.0, 21.0));
    CHECK(!db.contains(0, 52.05, 21.0));
    CHECK(db.contains(1, 52.12, 21.05));
    CHECK(!db.contains(1, 52.2, 21.05));
    // P zone: half circle (90->270 ccw = through north) closed by DP
    CHECK(db.contains(2, 52.31, 20.5));
    CHECK(!db.contains(2, 52.28, 20.5));

    // proximity check: inside CTR below ceiling -> violation
    AirspaceDb::Query q{52.0, 21.0, 400.f, 100.f, 1013.25f, 0.f, 0.f, 36000};
    auto hits = db.check(q);
    CHECK(!hits.empty() && hits[0].idx == 0 && hits[0].level == 2 && hits[0].insideH);
    // above CTR -> not a violation, but vertical margin known
    q.altMsl = 1000;
    hits = db.check(q);
    CHECK(!hits.empty() && hits[0].level == 0 && hits[0].vpos == VertPos::Above);
    NEAR(hits[0].vertMarginM, 1000 - 762, 1);
    // approaching EPTR77 from south at 15 m/s, 1.5 km away -> entry in ~100 s
    LatLon start = destination({52 + 5.0 / 60, 21.05}, 180, 1500);
    AirspaceDb::Query q2{start.lat, start.lon, 600.f, 100.f, 1013.25f, 0.f, 15.f, 36000};
    q2.lookAheadS = 120;
    q2.horizWarnS = 120;
    hits = db.check(q2);
    const AirspaceHit* tra = nullptr;
    for (auto& h : hits)
        if (h.idx == 1) tra = &h;
    CHECK(tra != nullptr);
    if (tra) {
        NEAR(tra->distM, 1500, 15);
        NEAR(tra->timeToEntryS, 100, 2);
        CHECK(tra->level == 1);
        NEAR(wrap180(tra->bearingDeg), 0, 1);
    }

    // activations
    FILE* f = std::fopen("/tmp/paranav_act.txt", "w");
    std::fprintf(f, "# test\ndate=2026-11-16\nEPTR77 0800-1200 1300-1400\nEPTR7 0000-2359\n");
    std::fclose(f);
    CHECK(db.loadActivations("/tmp/paranav_act.txt") == 1);  // EPTR7 must not match EPTR77
    CHECK(db.activationNow(z[1], 9 * 3600, 20261116) == Activation::Active);
    CHECK(db.activationNow(z[1], 12 * 3600 + 30 * 60, 20261116) == Activation::Inactive);
    CHECK(db.activationNow(z[1], 9 * 3600, 20261117) == Activation::Unknown);
    CHECK(db.activationNow(z[0], 9 * 3600, 20261116) == Activation::Permanent);
    // inactive zone never raises a warning
    q2.utcSec = 12 * 3600 + 30 * 60;
    q2.dateYmd = 20261116;
    hits = db.check(q2);
    for (auto& h : hits)
        if (h.idx == 1) CHECK(h.level == 0 && !h.active);
}

static void testLoggerAndCup() {
    LogPoint p{2026, 11, 16, 9, 5, 7, 52.5, 20.85, true, 512.4f, 530.f, 520.f};
    CHECK(TrackLogger::igcBRecord(p) == "B0905075230000N02051000EA0051200530");
    p.lat = -0.5;
    p.lon = -0.25;
    p.pressAlt = -12;
    CHECK(TrackLogger::igcBRecord(p) == "B0905070030000S00015000WA-001200530");
    CHECK(TrackLogger::igcFileName(p, 3) == "2026-11-16-XPN-NAV-03.igc");

    WaypointDb db;
    int n = db.loadCupText(
        "name,code,country,lat,lon,elev,style,rwdir,rwlen,freq,desc\n"
        "\"Lad, Polna\",LP,PL,5205.400N,02048.300E,110.0m,3,90,200m,,\"x\"\n"
        "\"Hill\",H,PL,5210.000N,02100.000E,1000ft,1,,,,\n"
        "-----Related Tasks-----\n\"T\",,,,\n");
    CHECK(n == 2);
    CHECK(db.all()[0].name == "Lad, Polna" && db.all()[0].isLanding());
    NEAR(db.all()[0].lat, 52 + 5.4 / 60, 1e-9);
    NEAR(db.all()[1].elevM, 304.8, 0.01);
    CHECK(db.nearestLanding(52.2, 21.0) == 0);
}

static void testSettings() {
    Settings s;
    s.pilot = "Jan Kowalski";
    s.fuelFlowLph = 4.2f;
    s.flip180 = true;
    s.tzOffsetMin = 120;
    CHECK(s.save("/tmp/paranav_cfg.ini"));
    Settings t;
    CHECK(t.load("/tmp/paranav_cfg.ini"));
    CHECK(t.pilot == "Jan Kowalski" && t.flip180 && t.tzOffsetMin == 120);
    NEAR(t.fuelFlowLph, 4.2, 0.01);
    CHECK(t.isIgnoredClass("W") && !t.isIgnoredClass("R"));
}

static void testTerrain() {
    // tiny synthetic SRTM3 tile: elevation = row index (north edge row 0)
    FILE* f = std::fopen("/tmp/N10E010.hgt", "wb");
    for (int r = 0; r < 1201; r++)
        for (int c = 0; c < 1201; c++) {
            uint8_t b[2] = {(uint8_t)(r >> 8), (uint8_t)(r & 0xFF)};
            std::fwrite(b, 1, 2, f);
        }
    std::fclose(f);
    Terrain t;
    t.setDir("/tmp");
    NEAR(t.elevation(10.999999, 10.5), 0, 0.01);  // north edge
    NEAR(t.elevation(10.5, 10.5), 600, 0.01);   // middle row
    NEAR(t.elevation(10.0001, 10.2), 1199.88, 0.05);
    CHECK(std::isnan(t.elevation(20.5, 10.5)));  // missing tile
    // tiles in sub-folders with lower-case names (as unpacked from viewfinderpanoramas zips)
    (void)!std::system("rm -rf /tmp/paranav_terr && mkdir -p /tmp/paranav_terr/M10 && cp /tmp/N10E010.hgt /tmp/paranav_terr/M10/n10e010.hgt");
    Terrain t2;
    t2.setDir("/tmp/paranav_terr");
    CHECK(t2.countTiles() == 1);
    NEAR(t2.elevation(10.5, 10.5), 600, 0.01);
}

static void testFlight() {
    Settings s;
    s.fuelStartL = 10;
    s.fuelFlowLph = 3.6f;  // 1 ml/s
    s.autoQnh = false;
    FlightComputer fc(s);
    BaroSnapshot b;
    b.ok = true;
    b.pressHpa = 1000;
    b.altStd = baroAltitude(1000, 1013.25f);
    GpsFix g;
    g.valid = true;
    g.sats = 10;
    g.hdop = 0.9f;
    g.lat = 52;
    g.lon = 21;
    g.year = 2026; g.month = 11; g.day = 16;
    uint32_t t = 100;
    for (int i = 0; i < 5; i++) fc.update(g, b, t++);
    CHECK(fc.state().homeSet && fc.state().phase == Phase::Ground);
    g.speedKmh = 36;
    g.courseDeg = 90;
    g.courseValid = true;
    for (int i = 0; i < 9; i++) {
        LatLon p = destination({g.lat, g.lon}, 90, 10);
        g.lat = p.lat; g.lon = p.lon;
        fc.update(g, b, t++);
    }
    CHECK(fc.state().phase == Phase::Ground);
    LatLon p = destination({g.lat, g.lon}, 90, 10);
    g.lat = p.lat; g.lon = p.lon;
    fc.update(g, b, t++);
    CHECK(fc.state().phase == Phase::Flying);
    CHECK(fc.takeoffEvent);
    for (int i = 0; i < 600; i++) {  // 10 min flying east at 36 km/h
        LatLon q = destination({g.lat, g.lon}, 90, 10);
        g.lat = q.lat; g.lon = q.lon;
        fc.update(g, b, t++);
    }
    const NavState& n = fc.state();
    NEAR(n.fuelUsedL, 3.6 * 610 / 3600.0, 0.02);
    NEAR(n.homeDistM, 6100, 20);
    NEAR(n.homeBrg, 270, 1);
    // ETE home with no wind at cruise 40 km/h
    NEAR(n.homeEteS, n.homeDistM / (40 / 3.6), 1);
    NEAR(n.returnNeedL, n.homeEteS / 3600 * 3.6 + 15.0 / 60 * 3.6, 0.01);
    // glide arrival with no wind: alt - dist/LD - elev
    float arr = fc.glideArrival(n.homeLat, n.homeLon, n.homeElev);
    NEAR(arr, n.altMsl - n.homeDistM / s.glideRatio - n.homeElev, 0.5);
    // landing
    g.speedKmh = 0;
    for (int i = 0; i < 61; i++) fc.update(g, b, t++);
    CHECK(fc.state().phase == Phase::Ground && fc.lastFlight.valid);
    NEAR(fc.lastFlight.distKm, 6.1, 0.1);
    // manual QNH via known altitude
    fc.setAltitude(250);
    fc.update(g, b, t++);
    NEAR(fc.state().altMsl, 250, 0.5);
}

static void testAlerts() {
    AlertManager a;
    CHECK(a.set(AL_AIRSPACE_NEAR, true, 10, "x", "y", 5));
    CHECK(a.top() == AL_AIRSPACE_NEAR);
    a.ack(AL_AIRSPACE_NEAR, 11);
    CHECK(a.top() == -1 && a.topAny() == AL_AIRSPACE_NEAR);
    CHECK(!a.set(AL_AIRSPACE_NEAR, true, 12, "x", "y", 5));
    CHECK(a.set(AL_AIRSPACE_NEAR, true, 13, "x", "y", 6));  // another zone -> new alert
    CHECK(a.set(AL_AIRSPACE_IN, true, 14, "in", "", 6));
    CHECK(a.top() == AL_AIRSPACE_IN);
    a.ack(AL_AIRSPACE_IN, 15);
    a.set(AL_AIRSPACE_IN, true, 100, "in", "", 6);
    CHECK(a.get(AL_AIRSPACE_IN).acked);
    CHECK(a.set(AL_AIRSPACE_IN, true, 200, "in", "", 6));  // re-armed after 120 s
}

static void testBlueFly() {
    BlueFlyParser p;
    // PRS stream, fragmented like BLE notifications (20 bytes)
    std::string s = "PRS 18A92\nPRS 18A9\x33\nBFV 12\n_PRS 18A90\r\n";
    int samples = 0;
    float last = 0;
    for (char c : s)
        if (p.feed(c)) {
            samples++;
            last = p.sample().pressureHpa;
        }
    CHECK(samples == 3);
    NEAR(last, 0x18A90 / 100.0, 1e-3);
    CHECK(std::string(p.format()) == "_PRS");
    // LK8EX1 with checksum: pressure in Pa, battery as percent+1000
    std::string lk = withCs("LK8EX1,98765,99999,-12,21.5,1085") + "\n";
    samples = 0;
    for (char c : lk)
        if (p.feed(c)) samples++;
    CHECK(samples == 1 && p.sample().kind == BlueFlyParser::Kind::Pressure);
    NEAR(p.sample().pressureHpa, 987.65, 1e-3);
    CHECK(p.batteryPct() == 85);
    NEAR(p.temperatureC(), 21.5, 1e-3);
    // LK8EX1 without pressure -> altitude
    CHECK(p.parseLine(withCs("LK8EX1,999999,1234,50,20,3.95").c_str()));
    CHECK(p.sample().kind == BlueFlyParser::Kind::Altitude && p.sample().altStd == 1234);
    NEAR(p.batteryVolts(), 3.95, 1e-3);
    // LXWP0 altitude
    CHECK(p.parseLine(withCs("LXWP0,N,,456.7,1.2,,,,,,,,").c_str()));
    NEAR(p.sample().altStd, 456.7, 1e-3);
    // bad checksum, GPS sentences and junk are ignored
    CHECK(!p.parseLine("$LK8EX1,98765,99999,-12,21.5,1085*00"));
    CHECK(!p.parseLine(withCs("GPRMC,101530.00,A,5212.3456,N,02051.0000,E,24.3,045.5,161126,,,A").c_str()));
    CHECK(!p.parseLine("PRS zzz"));
    CHECK(p.parseLine(withCs("XCTOD,77,19").c_str()) == false && p.batteryPct() == 77);
}

static void testGpsVario() {
    // no barometer: altitude and vario come from GPS
    Settings s;
    s.autoQnh = false;
    FlightComputer fc(s);
    BaroSnapshot none;  // ok = false
    GpsFix g;
    g.valid = true; g.sats = 9; g.hdop = 1; g.lat = 52; g.lon = 21; g.year = 2026; g.month = 1; g.day = 1;
    float alt = 200;
    for (uint32_t t = 1; t <= 60; t++) {
        alt += 2.0f;
        g.altMsl = alt;
        fc.update(g, none, t);
    }
    CHECK(fc.state().altSource == AltSource::Gps);
    NEAR(fc.state().altMsl, alt, 0.01);
    NEAR(fc.state().vario, 2.0, 0.4);
    BaroSnapshot b;
    b.ok = true;
    b.source = AltSource::BlueFly;
    b.altStd = 500;
    fc.update(g, b, 61);
    CHECK(fc.state().altSource == AltSource::BlueFly);
    // Kalman restarts after a long gap instead of using a bogus dt
    KalmanVario kv;
    kv.update(100, 0.02f);
    kv.update(300, 10.0f);
    NEAR(kv.altitude(), 300, 0.01);
    NEAR(kv.vario(), 0, 0.01);
}

static void testFileXfer() {
    (void)!std::system("rm -rf /tmp/pn_root && mkdir -p /tmp/pn_root/airspace /tmp/pn_root/waypoints");
    // an old airspace file that "replace" must remove; activations.txt must survive
    FILE* f = std::fopen("/tmp/pn_root/airspace/old.txt", "w");
    std::fputs(kOpenAir, f);
    std::fclose(f);
    f = std::fopen("/tmp/pn_root/airspace/activations.txt", "w");
    std::fputs("date=2026-11-16\n", f);
    std::fclose(f);

    std::vector<std::string> out;
    FileReceiver rx("/tmp/pn_root");
    rx.setNotify([&](const std::string& s) { out.push_back(s); });
    std::string data = std::string(kOpenAir) + std::string(kOpenAir);  // 6 zones, > 1 window? no - make it bigger
    while (data.size() < 10000) data += "* padding comment line to make the file longer than one window\n";
    uint32_t crc = crc32Update(0, (const uint8_t*)data.data(), data.size());
    CHECK(crc32Update(0, (const uint8_t*)"123456789", 9) == 0xCBF43926u);  // standard check value
    char cmd[128];
    std::snprintf(cmd, sizeof cmd, "BEGIN airspace %zu %08x 1 Łódź strefy.txt", data.size(), crc);
    rx.onCtrl(cmd);
    CHECK(!out.empty() && out.back() == "OK BEGIN 4096");
    auto chunk = [&](uint32_t off, size_t len) {
        std::vector<uint8_t> b(4 + len);
        b[0] = off & 0xFF; b[1] = (off >> 8) & 0xFF; b[2] = (off >> 16) & 0xFF; b[3] = off >> 24;
        std::memcpy(b.data() + 4, data.data() + off, len);
        rx.onData(b.data(), b.size());
    };
    uint32_t off = 0;
    int acks = 0;
    while (off < data.size()) {
        size_t len = std::min<size_t>(240, data.size() - off);
        chunk(off, len);
        off += (uint32_t)len;
        if (off == 2400) {  // simulate a lost packet: skip one, device must ask to resend
            chunk(off + 240, 240);
            CHECK(out.back() == "ERR SEQ 2400");
        }
    }
    for (auto& s : out) if (s.rfind("ACK ", 0) == 0) acks++;
    CHECK(acks == 3);  // 4096, 8192, end
    CHECK(out.back() == "ACK " + std::to_string(data.size()));
    rx.onCtrl("END");
    CHECK(out.back() == "OK RECEIVED");
    FileReceiver::Pending p;
    CHECK(rx.takePending(p) && p.kind == "airspace" && p.replace);
    std::string msg;
    int count = 0;
    CHECK(FileReceiver::install("/tmp/pn_root", p, msg, count));
    CHECK(count == 6);
    CHECK(msg == "airspace/Lodz_strefy.txt");
    FILE* chk = std::fopen("/tmp/pn_root/airspace/old.txt", "r");
    CHECK(chk == nullptr);  // replaced
    if (chk) std::fclose(chk);
    chk = std::fopen("/tmp/pn_root/airspace/activations.txt", "r");
    CHECK(chk != nullptr);  // kept
    if (chk) std::fclose(chk);

    // wrong CRC is rejected, nothing installed
    out.clear();
    rx.onCtrl("BEGIN waypoints 10 deadbeef 0 a.cup");
    uint8_t pkt[14] = {0, 0, 0, 0, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    rx.onData(pkt, sizeof pkt);
    rx.onCtrl("END");
    CHECK(out.back() == "ERR CRC");
    CHECK(!rx.takePending(p));
    // a file that is not OpenAir is rejected at install time
    std::string junk = "hello world\n";
    std::snprintf(cmd, sizeof cmd, "BEGIN airspace %zu %08x 0 junk.txt", junk.size(),
                  crc32Update(0, (const uint8_t*)junk.data(), junk.size()));
    rx.onCtrl(cmd);
    std::vector<uint8_t> b(4 + junk.size(), 0);
    std::memcpy(b.data() + 4, junk.data(), junk.size());
    rx.onData(b.data(), b.size());
    rx.onCtrl("END");
    CHECK(rx.takePending(p));
    CHECK(!FileReceiver::install("/tmp/pn_root", p, msg, count));
    // path safety
    out.clear();
    rx.onCtrl("DEL ../config.ini");
    CHECK(out.back() == "ERR PATH");
    rx.onCtrl("DEL airspace/../../x");
    CHECK(out.back() == "ERR PATH");
    rx.onCtrl("BEGIN firmware 10 0 0 x.bin");
    CHECK(out.back() == "ERR KIND");
    rx.onCtrl("BEGIN airspace 99999999 0 0 x.txt");
    CHECK(out.back() == "ERR SIZE");
    rx.onCtrl("LIST");
    CHECK(out.back() == "OK LIST 2");
    CHECK(FileReceiver::sanitize("../../.hidden name!.TXT", ".txt") == "hidden_name.txt");
}

// ---------------------------------------------------------------- topo map
static std::vector<uint8_t> packbitsEnc(const uint8_t* d, size_t n) {
    std::vector<uint8_t> o;
    size_t i = 0;
    while (i < n) {
        size_t r = 1;
        while (i + r < n && d[i + r] == d[i] && r < 128) r++;
        if (r >= 3) {
            o.push_back((uint8_t)(257 - r));
            o.push_back(d[i]);
            i += r;
            continue;
        }
        size_t s = i, k = 0;
        while (i < n && k < 128) {
            size_t rr = 1;
            while (i + rr < n && d[i + rr] == d[i] && rr < 3) rr++;
            if (rr >= 3) break;
            i++;
            k++;
        }
        o.push_back((uint8_t)(k - 1));
        o.insert(o.end(), d + s, d + s + k);
    }
    return o;
}

static void testTopo() {
    using namespace nav;
    // projection round trip + known value (tile of Gdansk at z14)
    double px, py, la, lo;
    topoToPixel(54.35, 18.65, 14, px, py);
    CHECK((int)(px / 256) == 9040 && (int)(py / 256) == 5233);
    topoToLatLon(px, py, 14, la, lo);
    CHECK(std::fabs(la - 54.35) < 1e-7 && std::fabs(lo - 18.65) < 1e-7);
    CHECK(std::fabs(topoMetresPerPixel(54.35, 14) - 5.57) < 0.05);

    // decoder: plain and XOR-8 records
    std::vector<uint8_t> bmp(kTopoTileBytes);
    for (int y = 0; y < 256; y++)
        for (int b = 0; b < 32; b++) bmp[y * 32 + b] = (y % 8 == 0) ? 0xFE : (uint8_t)((y * 7 + b * 13) % 251 == 0 ? 0x81 : 0);
    uint32_t crc = crc32Update(0, bmp.data(), bmp.size());
    for (int mode = 0; mode < 2; mode++) {
        std::vector<uint8_t> src = bmp;
        if (mode == 1)
            for (int k = kTopoTileBytes - 1; k >= 256; k--) src[k] ^= bmp[k - 256];
        std::vector<uint8_t> rec = {(uint8_t)mode, 0, 0, 0, (uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16),
                                    (uint8_t)(crc >> 24)};
        auto pk = packbitsEnc(src.data(), src.size());
        rec.insert(rec.end(), pk.begin(), pk.end());
        std::vector<uint8_t> out(kTopoTileBytes, 0x55);
        CHECK(topoDecodeTile(rec.data(), rec.size(), out.data()));
        CHECK(out == bmp);
        rec[20] ^= 1;  // corruption -> CRC fails (or structure error)
        CHECK(!topoDecodeTile(rec.data(), rec.size(), out.data()));
        CHECK(!topoDecodeTile(rec.data(), 5, out.data()));
    }

    // map folder made by tools/topo/make_tiles.py (optional: PARANAV_TOPO_DIR=<out>/maps)
    const char* dir = std::getenv("PARANAV_TOPO_DIR");
    if (dir) {
        TopoMap m;
        m.setDir(dir);
        CHECK(m.scan() >= 1);
        const TopoArea& a = m.areas()[0];
        CHECK(m.areaFor(a.lat0, a.lon0) == 0);
        CHECK(m.areaFor(a.lat0 + 5, a.lon0) == -1);
        int ok = 0, white = 0, nodata = 0, err = 0;
        for (int z = kTopoMinZoom; z <= kTopoMaxZoom; z++) {
            if (!a.hasZoom(z)) continue;
            double cx, cy;
            topoToPixel(a.lat0, a.lon0, z, cx, cy);
            int r = 3 + (z - 10) * 4;
            for (int ty = (int)(cy / 256) - r; ty <= (int)(cy / 256) + r; ty++)
                for (int tx = (int)(cx / 256) - r; tx <= (int)(cx / 256) + r; tx++) {
                    const uint8_t* b;
                    switch (m.tile(0, z, tx, ty, &b)) {
                        case TopoTile::Ok: ok++; CHECK(b != nullptr); break;
                        case TopoTile::White: white++; break;
                        case TopoTile::NoData: nodata++; break;
                        case TopoTile::Error: err++; break;
                    }
                }
        }
        std::printf("topo: %d tiles ok, %d white, %d no data, %d errors\n", ok, white, nodata, err);
        CHECK(ok > 50 && err == 0 && m.errors() == 0);
        const uint8_t* b1;
        double cx, cy;
        topoToPixel(a.lat0, a.lon0, 12, cx, cy);
        CHECK(m.tile(0, 12, (int)(cx / 256), (int)(cy / 256), &b1) != TopoTile::Error);
    }
}

int main() {
    testTopo();
    testFileXfer();
    testBlueFly();
    testGpsVario();
    testNmea();
    testGeo();
    testKalman();
    testWind();
    testAirspace();
    testLoggerAndCup();
    testSettings();
    testTerrain();
    testFlight();
    testAlerts();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
