// Desktop preview: simulates a short flight and renders every screen to PGM files.
// Build: see tools/preview/build.sh
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "airspace.h"
#include "alerts.h"
#include "flight.h"
#include "geo.h"
#include "terrain.h"
#include "topo.h"
#include "i18n.h"
#include "fanet.h"
#include "flightlog.h"
#include "logger.h"
#include "ui/canvas.h"
#include "ui/menu.h"
#include "ui/screens.h"
#include "waypoints.h"
#include "../../src/config.h"  // PARANAV_VERSION (only #defines)

using namespace nav;

static uint8_t fb[ui::W * ui::H / 2];

static void savePgm(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P5\n%d %d\n255\n", ui::W, ui::H);
    for (int y = 0; y < ui::H; y++)
        for (int x = 0; x < ui::W; x++) {
            uint8_t b = fb[y * ui::W / 2 + x / 2];
            uint8_t v = (x & 1) ? (b >> 4) : (b & 0xF);
            std::fputc(v * 17, f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (const char* l = std::getenv("PARANAV_LANG"))
        if (std::string(l) == "en") nav::setLang(nav::Lang::EN);
    std::string sd = argc > 1 ? argv[1] : "sd_card";
    std::string out = argc > 2 ? argv[2] : "/tmp/preview";
    std::string terrDir = argc > 3 ? argv[3] : "/tmp";

    Settings set;
    set.fuelStartL = 8;
    AirspaceDb asp;
    int nz = asp.loadOpenAir((sd + "/airspace/EXAMPLE_airspace.txt").c_str());
    int na = asp.loadActivations((sd + "/airspace/activations.txt").c_str());
    WaypointDb wp;
    wp.loadDir((sd + "/waypoints").c_str());
    Route route;
    route.loadCup((sd + "/route/example_route.cup").c_str());
    Terrain terr;
    terr.setDir(terrDir);
    std::printf("zones %d act %d wpts %zu route %zu\n", nz, na, wp.all().size(), route.pts.size());

    FlightComputer fc(set);
    fc.setTerrain(&terr);
    fc.setWaypoints(&wp);
    fc.setRoute(&route);
    AlertManager alerts;

    // simulation
    double lat = 52.0900, lon = 20.8500;
    float alt = 111;  // true MSL
    const float qnhTrue = 1021.f;
    float heading = 30, air = 40;  // km/h
    const float windFrom = 250, windSpd = 16;
    float wte, wtn;
    polarToVec(windFrom + 180, windSpd, wte, wtn);
    KalmanVario kv;
    std::vector<AirspaceHit> hits;
    int endT = argc > 4 ? std::atoi(argv[4]) : 900;
    for (int t = 0; t < endT; t++) {
        float climb = 0;
        bool flying = t >= 20;
        if (flying) {
            if (t < 260) climb = 1.6f;
            else if (t < 300) climb = 0.2f;
            if (t >= 300 && t < 330) heading += 12;          // 360° turn -> wind estimate
            else if (t >= 330 && t < 345) heading += 8;
            else if (t >= 345 && t < 560) climb = 0.0f;
            else if (t >= 560) { climb = -1.6f; }            // engine off glide
            if (t >= 600 && t < 690) { heading += 9; climb = 1.2f; }  // thermal circling
        }
        heading = (float)wrap360(heading);
        float ve = 0, vn = 0;
        if (flying) {
            float ae, an;
            polarToVec(heading, air, ae, an);
            ve = ae + wte;
            vn = an + wtn;
        }
        LatLon p = destination({lat, lon}, vecDir(ve, vn), std::hypot(ve, vn) / 3.6);
        lat = p.lat;
        lon = p.lon;
        alt += climb;
        if (t >= 560 && t < 600) fc.setEngine(false);

        GpsFix g;
        g.valid = true;
        g.quality = 1;
        g.sats = 11;
        g.hdop = 0.8f;
        g.lat = lat;
        g.lon = lon;
        g.altMsl = alt + 3;
        g.speedKmh = std::hypot(ve, vn);
        g.courseDeg = vecDir(ve, vn);
        g.courseValid = true;
        int sec = 10 * 3600 + 5 * 60 + t;
        g.year = 2026; g.month = 11; g.day = 16;
        g.hour = sec / 3600; g.minute = (sec / 60) % 60; g.second = sec % 60;

        BaroSnapshot b;
        b.ok = true;
        float pr = qnhTrue * std::pow(1 - alt / 44330.77f, 5.25588f);
        b.pressHpa = pr;
        b.tempC = 12;
        for (int k = 0; k < 50; k++) kv.update(baroAltitude(pr, 1013.25f) + ((k * 7919) % 13 - 6) * 0.03f, 0.02f);
        b.altStd = kv.altitude();
        b.vario = kv.vario();
        fc.update(g, b, (uint32_t)t + 5);

        const NavState& n = fc.state();
        AirspaceDb::Query q{n.lat, n.lon, n.altMsl, n.groundMsl, set.qnhHpa, n.trackDeg, n.gsKmh / 3.6f, n.utcSec};
        q.dateYmd = n.dateYmd;
        hits = asp.check(q, &set);
    }
    const NavState& n = fc.state();
    std::printf("alt %.0f agl %.0f qnh %.1f wind %d %.0f/%.0f ld %.1f home %.0fm reach %d fuel %.2f\n", n.altMsl,
                n.aglTerrain, set.qnhHpa, n.wind.valid, n.wind.speedKmh, n.wind.fromDeg, n.ldCurrent, n.homeDistM,
                n.homeReachable, n.fuelLeftL);

    std::printf("glide range 70: %.0f  250: %.0f\n", fc.glideRangeM(70), fc.glideRangeM(250));
    // alerts like the firmware would raise them
    if (!hits.empty() && hits[0].level == 2)
        alerts.set(AL_AIRSPACE_IN, true, 1, TR("W STREFIE: ", "INSIDE: ") + asp.zones()[hits[0].idx].name, TR("jesteś w środku", "you are inside"), hits[0].idx);
    else if (!hits.empty() && hits[0].level == 1)
        alerts.set(AL_AIRSPACE_NEAR, true, 1, TR("STREFA: ", "AIRSPACE: ") + asp.zones()[hits[0].idx].name,
                   TR("wlot za ", "entry in ") + ui::fmtDur(hits[0].timeToEntryS, false), hits[0].idx);

    ui::SysInfo sys;
    sys.battPct = 76; sys.battV = 3.94f; sys.battMa = -112; sys.battMinLeft = 540;
    sys.sdOk = true; sys.sdFreeMb = 14800;
    sys.airspaceZones = (int)asp.zones().size(); sys.airspacePoints = (int)asp.pointCount();
    sys.activationEntries = na; sys.activationDate = asp.activationDate();
    sys.waypoints = (int)wp.all().size(); sys.routePoints = (int)route.pts.size(); sys.terrainTiles = 1;
    sys.gpsModule = "u-blox MIA-M10Q"; sys.baroOk = true; sys.battSource = "BQ27220"; sys.backlight = true; sys.bfState = 3; sys.bfName = "BlueFly-7970"; sys.bfBattPct = 85; sys.bfRssi = -62; sys.i2cDevices = "20 51 55 5D 68 6B 77"; sys.recording = true;
    sys.logFile = "/sd/logs/2026-11-16-XPN-NAV-01.igc";
    sys.freeHeapKb = 180; sys.freePsramKb = 7600;
    sys.psramFreeBlocks = 140; sys.renderMaxMs = 310;  // typical values on the device (diagnostics row)
    sys.version = PARANAV_VERSION;

    ui::UiContext u;
    u.nav = &n; u.set = &set; u.asp = &asp; u.hits = &hits; u.wpts = &wp; u.route = &route;
    u.fc = &fc; u.terrain = &terr; u.alerts = &alerts; u.sys = &sys;
    // FANET traffic around the simulated position
    TrafficDb traffic;
    {
        struct T { uint32_t a; float brg, dist, dz, hdg, kmh, climb; const char* name; int age; bool ground; };
        // hdg is relative to our track (180 = flying towards us)
        T ts[] = {{0x011234, 40, 380, 25, 220, 38, 0.8f, "Ola Skytraxx", 1, false},
                  {0x20ABCD, 300, 1800, -140, 90, 32, -1.2f, "Marek", 3, false},
                  {0x110042, 160, 3900, 420, 10, 45, 2.5f, "", 25, false},
                  {0xFD7777, 210, 1200, 0, 0, 0, 0, "Kierowca", 8, true}};
        for (auto& t : ts) {
            LatLon q = destination({n.lat, n.lon}, n.trackDeg + t.brg, t.dist);
            FanetPacket p;
            p.type = t.ground ? FT_GROUND : FT_TRACKING;
            p.src = t.a; p.hasPos = true; p.lat = q.lat; p.lon = q.lon; p.altM = n.gpsAlt + t.dz;
            p.headingDeg = (float)wrap360(n.trackDeg + t.hdg); p.speedKmh = t.kmh; p.climbMs = t.climb; p.aircraft = FA_PARAGLIDER;
            traffic.onPacket(p, 100000 - t.age * 1000, -95);
            if (*t.name) {
                FanetPacket nm; nm.type = FT_NAME; nm.src = t.a; nm.name = t.name;
                traffic.onPacket(nm, 100000, -95);
            }
        }
        TrafficOwn me;
        me.valid = true; me.flying = true; me.lat = n.lat; me.lon = n.lon; me.altM = n.gpsAlt;
        me.speedKmh = n.gsKmh; me.trackDeg = n.trackDeg; me.climbMs = n.vario1s;
        traffic.evaluate(me, 100000);
    }
    u.traffic = &traffic.list();
    sys.fanetState = 2; sys.fanetId = "FD3C9A"; sys.fanetTxCount = 182; sys.fanetRxCount = 57;
    // background map: PARANAV_TOPO_DIR=<out>/maps from tools/topo/make_tiles.py
    TopoMap topo;
    TopoView tv;
    const char* td = std::getenv("PARANAV_TOPO_DIR");
    topo.setDir(td ? td : sd + "/maps");
    sys.topoAreas = topo.scan();
    u.topo = &topo; u.topoView = &tv;
    if (n.fix && topo.areaFor(n.lat, n.lon) >= 0) sys.topoArea = topo.areas()[topo.areaFor(n.lat, n.lon)].name;

    ui::Canvas c(fb);
    std::string cmd = "mkdir -p " + out;
    (void)!std::system(cmd.c_str());
    for (int s = 0; s < ui::SCR_COUNT; s++) {
        ui::drawScreen(c, (ui::ScreenId)s, u);
        char name[64];
        std::snprintf(name, sizeof name, "/%d_%s.pgm", s, ui::screenName((ui::ScreenId)s));
        savePgm(out + name);
    }
    for (int z : {11, 12, 14}) {
        set.topoZoom = z;
        ui::drawScreen(c, ui::SCR_TOPO, u);
        savePgm(out + "/T_topo_z" + std::to_string(z) + ".pgm");
    }
    set.topoZoom = 13;
    {
        TopoMap none;
        none.setDir("/nonexistent");
        none.scan();
        u.topo = &none;
        ui::drawScreen(c, ui::SCR_TOPO, u);
        savePgm(out + "/T_topo_nomap.pgm");
        u.topo = &topo;
    }
    ui::drawUsbDisk(c, u, true);
    savePgm(out + "/U_usb.pgm");
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    ui::drawTrafficHint(c, u);
    savePgm(out + "/R_traffic_hint.pgm");
    // alert overlay example
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    int a = alerts.top();
    if (a >= 0) {
        ui::drawAlertBanner(c, u, a);
        savePgm(out + "/8_alert.pgm");
    }
    // menu example
    auto root = std::make_shared<ui::MenuItem>(ui::MenuItem::sub("root", {
        ui::MenuItem::sub(TR("Lot", "Flight"), {}),
        ui::MenuItem::sub(TR("Paliwo", "Fuel"), {}),
        ui::MenuItem::sub(TR("Wysokościomierz", "Altimeter"), {}),
        ui::MenuItem::sub(TR("Nawigacja", "Navigation"), {}),
        ui::MenuItem::sub(TR("Ekran i mapa", "Display and map"), {}),
        ui::MenuItem::sub(TR("Alarmy", "Alarms"), {}),
        ui::MenuItem::sub("System", {}),
    }));
    root->children[1].children = {
        ui::MenuItem::num(TR("Paliwo na starcie", "Fuel at takeoff"), [&] { return set.fuelStartL; }, [&](float v) { set.fuelStartL = v; }, 0, 30, 0.5f, 1, "l"),
        ui::MenuItem::num(TR("Zużycie", "Fuel flow"), [&] { return set.fuelFlowLph; }, [&](float v) { set.fuelFlowLph = v; }, 0.5f, 15, 0.1f, 1, "l/h"),
    };
    ui::Menu m;
    m.open(root);
    m.handleKey(ui::Key::NextShort);
    m.draw(c);
    savePgm(out + "/9_menu.pgm");
    m.handleKey(ui::Key::NextLong);
    m.handleKey(ui::Key::NextLong);
    m.handleKey(ui::Key::NextShort);
    m.draw(c);
    savePgm(out + "/9b_menu_edit.pgm");
    fc.lastFlight.valid = true;
    ui::drawSummary(c, u);
    savePgm(out + "/A_summary.pgm");
    sys.phoneState = 3; sys.phoneProgress = 0.62f; sys.phoneFile = "openaip_airspace_pl.txt";
    sys.phoneLast = TR("812 stref – airspace/openaip_airspace_pl.txt", "812 airspaces – airspace/openaip_airspace_pl.txt");
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    ui::drawTransferBanner(c, u);
    savePgm(out + "/E_receiving.pgm");
    sys.phoneState = 2;
    ui::drawScreen(c, ui::SCR_SYSTEM, u);
    savePgm(out + "/F_system_phone.pgm");
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    ui::drawPowerHint(c);
    savePgm(out + "/C_power_hint.pgm");
    ui::drawPowerOff(c, TR("WYŁĄCZONY", "POWERED OFF"), TR("Włączanie: przycisk PWR lub podłączenie USB", "Power on: PWR button or USB"));
    savePgm(out + "/D_power_off.pgm");
    // Screens around a background map (PARANAV_DEMO_MAPS=<out>/maps from make_tiles.py; the flight is
    // made up around the first map area): power-off background, TOPO while waiting for GPS, saved
    // flight viewer with a 1:20 h flight
    TopoMap demo;
    if (const char* dm = std::getenv("PARANAV_DEMO_MAPS")) {
        demo.setDir(dm);
        demo.scan();
    }
    if (!demo.areas().empty()) {
        const TopoArea& A = demo.areas()[0];
        u.topo = &demo;
        const double la0 = A.lat0 - 0.03, lo0 = A.lon0 - 0.09;
        ui::drawPowerOff(c, TR("WYŁĄCZONY", "POWERED OFF"), TR("Włączanie: przycisk PWR lub podłączenie USB", "Power on: PWR button or USB"), &demo, la0, lo0);
        savePgm(out + "/D_power_off_map.pgm");
        NavState ns = n;
        ns.fix = false;
        ns.gsKmh = 0;
        ui::UiContext u2 = u;
        u2.nav = &ns;
        u2.fc = nullptr;
        u2.lastLat = la0;
        u2.lastLon = lo0;
        TopoView tv2;
        u2.topoView = &tv2;
        set.topoZoom = 12;
        ui::drawScreen(c, ui::SCR_TOPO, u2);
        savePgm(out + "/T_topo_bez_gps.pgm");
        set.topoZoom = 13;
        // flight: loop east and back, thermals, 80 m takeoff
        (void)!std::system("rm -rf /tmp/pn_preview_logs && mkdir -p /tmp/pn_preview_logs");
        TrackLogger lg;
        const int T = 4816;
        auto at = [&](int t, LogPoint& p) {
            double uu = (double)t / T;
            double e = 9.5 * std::sin(2 * kPi * uu) + 1.6 * std::sin(6 * kPi * uu) + 0.06 * std::sin(t / 47.0);
            double nn = -5.0 * (1 - std::cos(2 * kPi * uu)) / 2 + 1.8 * std::sin(4 * kPi * uu) + 0.05 * std::cos(t / 39.0);
            p.lat = la0 + nn / 111.195;
            p.lon = lo0 + e / (111.195 * std::cos(la0 * kDeg2Rad));
            // climb out over 10 min, thermals, glide down over the last 9 min (continuous)
            float k = std::min(1.f, std::min(t / 600.f, (T - t) / 540.f));
            float a = 80 + (260 + 110 * (float)std::sin(2 * kPi * t / 900.0) + 40 * (float)std::sin(2 * kPi * t / 230.0)) * k;
            p.gpsAlt = a;
            p.pressAlt = a - 60;
            int s = 13 * 3600 + 5 * 60 + t;
            p.hour = (uint8_t)(s / 3600);
            p.minute = (uint8_t)((s / 60) % 60);
            p.second = (uint8_t)(s % 60);
        };
        LogPoint lp{2026, 10, 17, 13, 5, 0, la0, lo0, true, 0, 0, 0};
        at(0, lp);
        lg.start("/tmp/pn_preview_logs", lp, "P", "G", "ID", "GPS", PARANAV_VERSION);
        for (int t = 0; t <= T; t++) {
            at(t, lp);
            lg.add(lp);
        }
        lg.stop();
        auto files = listFlights("/tmp/pn_preview_logs");
        FlightLog fl;
        if (!files.empty() && loadFlight(files[0].path, fl)) {
            ui::FlightView fv;
            fv.log = &fl;
            fv.title = files[0].name;
            ui::flightViewFit(fv);
            ui::UiContext u3 = u;
            ui::drawFlightView(c, u3, fv);
            savePgm(out + "/G_lot.pgm");
            fv.cursor = (int)fl.pts.size() * 2 / 5;
            ui::drawFlightView(c, u3, fv);
            savePgm(out + "/G_lot_punkt.pgm");
            ui::flightViewZoom(fv, +1);
            ui::drawFlightView(c, u3, fv);
            savePgm(out + "/G_lot_zoom.pgm");
            fv.askDelete = true;
            ui::drawFlightView(c, u3, fv);
            savePgm(out + "/G_lot_usun.pgm");
        }
        u.topo = &topo;
    }
    ui::drawBoot(c, {TR("[OK] Wyświetlacz", "[OK] Display"), TR("[OK] Barometr MS5611", "[OK] Barometer MS5611"), TR("[OK] Karta SD", "[OK] SD card"), TR("[OK] Strefy: 4", "[OK] Airspace: 4"), TR("[..] GPS: szukam satelitów", "[..] GPS: searching for satellites")}, PARANAV_VERSION);
    savePgm(out + "/B_boot.pgm");
    return 0;
}
