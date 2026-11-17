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
#include "ui/canvas.h"
#include "ui/menu.h"
#include "ui/screens.h"
#include "waypoints.h"

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
    std::string sd = argc > 1 ? argv[1] : "sd_card";
    std::string out = argc > 2 ? argv[2] : "/tmp/preview";
    std::string terrDir = argc > 3 ? argv[3] : "/tmp";

    Settings set;
    set.fuelStartL = 8;
    AirspaceDb asp;
    int nz = asp.loadOpenAir((sd + "/airspace/PRZYKLAD_strefy.txt").c_str());
    int na = asp.loadActivations((sd + "/airspace/activations.txt").c_str());
    WaypointDb wp;
    wp.loadDir((sd + "/waypoints").c_str());
    Route route;
    route.loadCup((sd + "/route/trasa.cup").c_str());
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
        alerts.set(AL_AIRSPACE_IN, true, 1, "STREFA: " + asp.zones()[hits[0].idx].name, "jesteś w środku", hits[0].idx);
    else if (!hits.empty() && hits[0].level == 1)
        alerts.set(AL_AIRSPACE_NEAR, true, 1, "BLISKO: " + asp.zones()[hits[0].idx].name,
                   "wlot za " + ui::fmtDur(hits[0].timeToEntryS, false), hits[0].idx);

    ui::SysInfo sys;
    sys.battPct = 76; sys.battV = 3.94f; sys.battMa = -112; sys.battMinLeft = 540;
    sys.sdOk = true; sys.sdFreeMb = 14800;
    sys.airspaceZones = (int)asp.zones().size(); sys.airspacePoints = (int)asp.pointCount();
    sys.activationEntries = na; sys.activationDate = asp.activationDate();
    sys.waypoints = (int)wp.all().size(); sys.routePoints = (int)route.pts.size(); sys.terrainTiles = 1;
    sys.gpsModule = "u-blox MIA-M10Q"; sys.baroOk = true; sys.battSource = "BQ27220"; sys.backlight = true; sys.bfState = 3; sys.bfName = "BlueFly-7970"; sys.bfBattPct = 85; sys.bfRssi = -62; sys.i2cDevices = "20 51 55 5D 68 6B 77"; sys.recording = true;
    sys.logFile = "/sd/logs/2026-11-16-XPN-NAV-01.igc";
    sys.freeHeapKb = 180; sys.freePsramKb = 7600;

    ui::UiContext u;
    u.nav = &n; u.set = &set; u.asp = &asp; u.hits = &hits; u.wpts = &wp; u.route = &route;
    u.fc = &fc; u.terrain = &terr; u.alerts = &alerts; u.sys = &sys;
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
        savePgm(out + "/T_topo_brak.pgm");
        u.topo = &topo;
    }
    ui::drawUsbDisk(c, u, true);
    savePgm(out + "/U_usb.pgm");
    // alert overlay example
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    int a = alerts.top();
    if (a >= 0) {
        ui::drawAlertBanner(c, u, a);
        savePgm(out + "/8_alert.pgm");
    }
    // menu example
    auto root = std::make_shared<ui::MenuItem>(ui::MenuItem::sub("root", {
        ui::MenuItem::sub("Lot", {}),
        ui::MenuItem::sub("Paliwo", {}),
        ui::MenuItem::sub("Wysokościomierz", {}),
        ui::MenuItem::sub("Nawigacja", {}),
        ui::MenuItem::sub("Mapa", {}),
        ui::MenuItem::sub("Alarmy", {}),
        ui::MenuItem::sub("System", {}),
    }));
    root->children[1].children = {
        ui::MenuItem::num("Paliwo na starcie", [&] { return set.fuelStartL; }, [&](float v) { set.fuelStartL = v; }, 0, 30, 0.5f, 1, "l"),
        ui::MenuItem::num("Zużycie", [&] { return set.fuelFlowLph; }, [&](float v) { set.fuelFlowLph = v; }, 0.5f, 15, 0.1f, 1, "l/h"),
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
    sys.phoneLast = "812 stref – airspace/openaip_airspace_pl.txt";
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    ui::drawTransferBanner(c, u);
    savePgm(out + "/E_odbior.pgm");
    sys.phoneState = 2;
    ui::drawScreen(c, ui::SCR_SYSTEM, u);
    savePgm(out + "/F_system_tel.pgm");
    ui::drawScreen(c, ui::SCR_FLIGHT, u);
    ui::drawPowerHint(c);
    savePgm(out + "/C_power_hint.pgm");
    ui::drawPowerOff(c, "WYŁĄCZONY", "Włączanie: przycisk PWR lub podłączenie USB");
    savePgm(out + "/D_power_off.pgm");
    ui::drawBoot(c, {"[OK] Wyświetlacz", "[OK] Barometr MS5611", "[OK] Karta SD", "[OK] Strefy: 4", "[..] GPS: szukam satelitów"}, "0.1");
    savePgm(out + "/B_boot.pgm");
    return 0;
}
