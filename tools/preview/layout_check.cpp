// Layout checker: renders every screen in many (also extreme) situations and reports
//  - text outside the screen,
//  - text overlapping other text,
//  - text crossing separator lines / box borders.
// Build & run: tools/preview/layout_check.sh
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
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
#include "ui/canvas.h"
#include "ui/help.h"
#include "ui/menu.h"
#include "ui/screens.h"
#include "waypoints.h"

using namespace nav;

static uint8_t fb[ui::W * ui::H / 2];
static int problems = 0;

static void savePgm(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P5\n%d %d\n255\n", ui::W, ui::H);
    for (int y = 0; y < ui::H; y++)
        for (int x = 0; x < ui::W; x++) {
            uint8_t b = fb[y * ui::W / 2 + x / 2];
            std::fputc(((x & 1) ? (b >> 4) : (b & 0xF)) * 17, f);
        }
    std::fclose(f);
}

static bool overlap(const ui::DebugBox& a, const ui::DebugBox& b, int m = 0) {
    return a.x0 <= b.x1 - m && b.x0 <= a.x1 - m && a.y0 <= b.y1 - m && b.y0 <= a.y1 - m;
}

static void analyse(const std::string& where, const std::vector<ui::DebugBox>& boxes, bool mapMode) {
    std::set<std::string> seen;
    auto report = [&](const std::string& msg) {
        if (seen.insert(msg).second) {
            std::printf("  [%s] %s\n", where.c_str(), msg.c_str());
            problems++;
        }
    };
    for (size_t i = 0; i < boxes.size(); i++) {
        const auto& t = boxes[i];
        if (t.text.empty()) continue;
        if (t.x0 < 0 || t.x1 >= ui::W || t.y0 < 0 || t.y1 >= ui::H)
            report("off screen: \"" + t.text + "\" x=" + std::to_string(t.x0) + ".." + std::to_string(t.x1) +
                   " y=" + std::to_string(t.y0) + ".." + std::to_string(t.y1));
        for (size_t j = 0; j < boxes.size(); j++) {
            if (i == j) continue;
            const auto& o = boxes[j];
            if (o.text.empty()) {
                // separator crossing (1 px tolerance)
                if (overlap(t, o, 1)) report("text on a line: \"" + t.text + "\"" + (getenv("LCDBG") ? " " + std::to_string(t.x0) + "," + std::to_string(t.y0) + "-" + std::to_string(t.x1) + "," + std::to_string(t.y1) + " line " + std::to_string(o.x0) + "," + std::to_string(o.y0) + "-" + std::to_string(o.x1) + "," + std::to_string(o.y1) : std::string()));
            } else if (j > i && overlap(t, o, 1)) {
                if (mapMode) continue;  // map labels may overlap each other in dense areas
                report("overlap: \"" + t.text + "\" / \"" + o.text + "\"");
            }
        }
    }
}

struct World {
    Settings set;
    AirspaceDb asp;
    WaypointDb wp;
    Route route;
    Terrain terr;
    std::unique_ptr<FlightComputer> fc;
    AlertManager alerts;
    ui::SysInfo sys;
    std::vector<AirspaceHit> hits;
};

static void simulate(World& w, int seconds) {
    w.fc.reset(new FlightComputer(w.set));
    w.fc->setTerrain(&w.terr);
    w.fc->setWaypoints(&w.wp);
    w.fc->setRoute(&w.route);
    double lat = 52.09, lon = 20.85;
    float alt = 111, hdg = 30;
    for (int t = 0; t < seconds; t++) {
        bool fly = t >= 20;
        if (fly && t > 300 && t < 330) hdg += 12;
        LatLon p = fly ? destination({lat, lon}, hdg, 12) : LatLon{lat, lon};
        lat = p.lat;
        lon = p.lon;
        if (fly && t < 260) alt += 1.5f;
        GpsFix g;
        g.valid = true; g.sats = 10; g.hdop = 1; g.lat = lat; g.lon = lon; g.altMsl = alt;
        g.speedKmh = fly ? 43 : 0; g.courseDeg = hdg; g.courseValid = true;
        int sec = 36000 + t;
        g.year = 2026; g.month = 11; g.day = 16; g.hour = sec / 3600; g.minute = sec / 60 % 60; g.second = sec % 60;
        BaroSnapshot b;
        b.ok = true; b.pressHpa = 1013.25f * std::pow(1 - alt / 44330.77f, 5.255f); b.altStd = alt; b.vario = 1.2f;
        w.fc->update(g, b, (uint32_t)t + 1);
    }
}

static void render(World& w, const std::string& name) {
    ui::UiContext u;
    u.nav = &w.fc->state(); u.set = &w.set; u.asp = &w.asp; u.hits = &w.hits; u.wpts = &w.wp; u.route = &w.route;
    u.fc = w.fc.get(); u.terrain = &w.terr; u.alerts = &w.alerts; u.sys = &w.sys;
    static TopoMap topo;
    static bool topoInit = false;
    if (!topoInit) {
        const char* td = std::getenv("PARANAV_TOPO_DIR");
        topo.setDir(td ? td : "sd_card/maps");
        topo.scan();
        topoInit = true;
    }
    TopoView tv;
    u.topo = &topo; u.topoView = &tv;
    // FANET traffic with worst-case texts: long names, big numbers, all levels
    TrafficDb traffic;
    {
        const NavState& n = w.fc->state();
        struct T { uint32_t a; float brg, dist, dz, hdg, kmh, climb; const char* name; bool ground; };
        T ts[] = {{0x011234, 20, 120, 20, 180, 40, -12.3f, "Wiktoria Grzegorzewska-Żółć", false},
                  {0x20ABCD, 300, 280, -60, 120, 300, 12.3f, "", false},
                  {0x110042, 160, 19900, 4400, 10, 45, 2.5f, "Maksymilian Brzęczyszczykiewicz", false},
                  {0xFD7777, 210, 9999, 0, 0, 0, 0, "Kierowca busa ekipy zawodów", true},
                  {0xFD0001, 90, 2500, -9999, 270, 20, 0, "X", false}};
        for (auto& t : ts) {
            LatLon q = destination({n.lat, n.lon}, n.trackDeg + t.brg, t.dist);
            FanetPacket p;
            p.type = t.ground ? FT_GROUND : FT_TRACKING;
            p.src = t.a; p.hasPos = true; p.lat = q.lat; p.lon = q.lon; p.altM = n.gpsAlt + t.dz;
            p.headingDeg = (float)wrap360(n.trackDeg + t.hdg); p.speedKmh = t.kmh; p.climbMs = t.climb;
            traffic.onPacket(p, 50000, -100);
            if (*t.name) { FanetPacket nm; nm.type = FT_NAME; nm.src = t.a; nm.name = t.name; traffic.onPacket(nm, 50000, -100); }
        }
        TrafficOwn me;
        me.valid = true; me.flying = true; me.lat = n.lat; me.lon = n.lon; me.altM = n.gpsAlt;
        me.speedKmh = n.gsKmh; me.trackDeg = n.trackDeg;
        traffic.evaluate(me, 59000);
    }
    u.traffic = &traffic.list();
    w.sys.fanetId = "FD3C9A"; w.sys.fanetState = 2; w.sys.fanetTxCount = 999999; w.sys.fanetRxCount = 999999;
    ui::Canvas c(fb);
    std::vector<ui::DebugBox> boxes;
    ui::Canvas::debugSink = &boxes;
    for (int s = 0; s < ui::SCR_COUNT; s++) {
        boxes.clear();
        ui::drawScreen(c, (ui::ScreenId)s, u);
        analyse(name + "/" + ui::screenName((ui::ScreenId)s), boxes, s == ui::SCR_MAP || s == ui::SCR_TOPO);
        savePgm("/tmp/layout/" + name + "_" + std::to_string(s) + ".pgm");
    }
    for (int z = 10; z <= 14; z += 2) {  // TOPO at other zoom levels
        int keep = w.set.topoZoom;
        w.set.topoZoom = z;
        boxes.clear();
        ui::drawScreen(c, ui::SCR_TOPO, u);
        analyse(name + "/TOPO-z" + std::to_string(z), boxes, true);
        w.set.topoZoom = keep;
    }
    boxes.clear();
    c.clear();
    ui::drawTrafficHint(c, u);
    analyse(name + "/RUCH-podpowiedz", boxes, false);
    boxes.clear();
    ui::drawUsbDisk(c, u, false);
    analyse(name + "/USB", boxes, false);
    for (int f = 0; f < 2; f++) {
        boxes.clear();
        ui::drawUsbAsk(c, u, f == 1, f, 15, 1);
        analyse(name + "/USB-PYTANIE" + std::to_string(f), boxes, false);
        savePgm("/tmp/layout/" + name + "_ask" + std::to_string(f) + ".pgm");
        if (ui::usbAskButtonAt(900, 300, f == 1) != (f == 1 ? 2 : 1) || ui::usbAskButtonAt(20, 300, f == 1) != 0 ||
            ui::usbAskButtonAt(500, 100, f == 1) != -1) {
            std::printf("  [USB-PYTANIE] wrong touch mapping\n");
            problems++;
        }
    }
    boxes.clear();
    ui::SysInfo keepSys = w.sys;
    w.sys.chgStat = 2; w.sys.battMinToFull = 999; w.sys.battRemMah = 1499; w.sys.battFullMah = 1500;
    if (w.sys.battV < 4.15f) w.sys.battV = 4.19f;
    ui::drawCharging(c, u);
    analyse(name + "/LADOWANIE", boxes, false);
    savePgm("/tmp/layout/" + name + "_charging.pgm");
    w.sys = keepSys;
    for (int a = 0; a < AL_COUNT; a++) {
        if (!w.alerts.get(a).active) continue;
        boxes.clear();
        c.clear();
        ui::drawAlertBanner(c, u, a);
        analyse(name + "/ALARM" + std::to_string(a), boxes, false);
    }
    if (w.sys.phoneState == 3) {  // transfer banner is an overlay: check it on its own
        boxes.clear();
        c.clear();
        ui::drawTransferBanner(c, u);
        analyse(name + "/ODBIÓR", boxes, false);
    }
    boxes.clear();
    ui::drawSummary(c, u);
    analyse(name + "/PODSUMOWANIE", boxes, false);
    savePgm("/tmp/layout/" + name + "_summary.pgm");
    {   // "LANDED?" question over the flight screen (extreme values in the extreme scenario)
        bool ext = name == "skrajne";
        NavState ln = *u.nav;
        ln.landingAsk = true;
        ln.landingStillS = ext ? 65535 : 65;
        ln.landingEndInS = ext ? 65535 : 235;
        if (!ext) { ln.gsKmh = 2; ln.vario1s = 0.1f; ln.aglTerrain = 18; }
        const NavState* keep = u.nav;
        u.nav = &ln;
        ui::drawScreen(c, ui::SCR_FLIGHT, u);
        boxes.clear();
        ui::drawLandingAsk(c, u);
        analyse(name + "/LADOWANIE", boxes, false);
        savePgm("/tmp/layout/" + name + "_landing.pgm");
        if (!std::isnan(ln.aglTerrain)) {  // also the "from takeoff" variant (no terrain data)
            ln.aglTerrain = NAN;
            boxes.clear();
            ui::drawLandingAsk(c, u);
            analyse(name + "/LADOWANIE-BEZ-TERENU", boxes, false);
        }
        u.nav = keep;
        if (ui::landingAskButtonAt(100, 210) != 0 || ui::landingAskButtonAt(800, 210) != 1 || ui::landingAskButtonAt(100, 100) != -1)
            std::printf("  [LADOWANIE] wrong button mapping\n"), problems++;
    }
    if (w.asp.zones().size() >= 2) {   // airspace exit screen: horizontal / up / down / outside, and the route check
        int nz = (int)w.asp.zones().size();
        EscapeResult e;
        e.inside = true;
        e.zones = {nz - 2, nz - 1, 0};
        e.horiz = {EscapeOption::Horizontal, true, 247, 12345, 0, 0, 5999};
        e.up = {EscapeOption::Up, true, 0, 0, 9999, 8888, 5999};
        e.down = {EscapeOption::Down, false, 0, 0, 0, 0, 0};
        const char* nm[5] = {"h", "up", "down", "out", "fl"};
        for (int k = 0; k < 5; k++) {
            EscapeResult x = e;
            if (k == 0) x.best = x.horiz;
            if (k == 1) x.best = x.up;
            if (k == 2) { x.down = {EscapeOption::Down, true, 0, 0, 120, -9999, 5999}; x.best = x.down; }
            if (k == 3) x.inside = false;
            if (k == 4) { x.up.valid = false; x.up.overLimit = true; x.best = x.horiz; }  // climbing would end above FL95
            u.escape = &x;
            boxes.clear();
            ui::drawEscape(c, u);
            analyse(name + "/WYJSCIE-" + nm[k], boxes, false);
            savePgm("/tmp/layout/" + name + "_esc_" + nm[k] + ".pgm");
        }
        u.escape = nullptr;
        std::vector<RouteConflict> rc;
        for (int i = 0; i < 9; i++) {
            RouteConflict r;
            r.zone = i % nz;
            r.atKm = 99.9f * i;
            r.act = (Activation)(i % 4);
            r.conflict = i % 3 != 2;
            rc.push_back(r);
        }
        boxes.clear();
        ui::drawRouteCheck(c, u, "przelot_dlugi_nazwa_trasy_2026_bardzo_dluga_nazwa.cup", rc);
        analyse(name + "/TRASA-KONTROLA", boxes, false);
        savePgm("/tmp/layout/" + name + "_routecheck.pgm");
        boxes.clear();
        ui::drawRouteCheck(c, u, "ok.cup", {});
        analyse(name + "/TRASA-OK", boxes, false);
        // alert with buttons over a screen
        ui::drawScreen(c, ui::SCR_FLIGHT, u);
        ui::drawAlertBanner(c, u, AL_AIRSPACE_IN);
        savePgm("/tmp/layout/" + name + "_alert_asp.pgm");
        if (ui::alertButtonAt(AL_AIRSPACE_IN, 100, 210) != ui::AB_ESCAPE || ui::alertButtonAt(AL_AIRSPACE_IN, 800, 210) != ui::AB_IGNORE ||
            ui::alertButtonAt(AL_AIRSPACE_IN, 100, 100) != ui::AB_NONE || ui::alertButtonAt(AL_TRAFFIC, 100, 210) != ui::AB_NONE) {
            std::printf("  [ALARM] wrong button mapping\n");
            problems++;
        }
    }
    ui::Canvas::debugSink = nullptr;
    {   // overlays on top of a screen, for looking at (not analysed together)
        ui::drawScreen(c, ui::SCR_FLIGHT, u);
        for (int a = 0; a < AL_COUNT; a++)
            if (w.alerts.get(a).active) { ui::drawAlertBanner(c, u, a); break; }
        ui::drawTransferBanner(c, u);
        ui::drawPowerHint(c);
        savePgm("/tmp/layout/" + name + "_overlay.pgm");
        ui::drawScreen(c, ui::SCR_NAV, u);
        ui::drawTrafficHint(c, u);
        savePgm("/tmp/layout/" + name + "_hint.pgm");
        ui::drawUsbDisk(c, u, true);
        savePgm("/tmp/layout/" + name + "_usb.pgm");
    }
}

static const char* kLongZones = R"(
AC R
AN EPTR123 BARDZO DŁUGA NAZWA STREFY TESTOWEJ ŻÓŁĆ
AY TRA
AL 2500ft AGL
AH FL195
DP 52:06:00 N 020:42:00 E
DP 52:09:00 N 020:47:00 E
DP 52:03:00 N 020:47:00 E
AC CTR
AN MCTR ZAKRZEWO-ŚWIERCZYNÓWEK MIĘDZYNARODOWE
AL 1500 ft AMSL
AH 12500ft AMSL
V X=52:05:00 N 020:51:00 E
DC 2
)";

int main(int argc, char** argv) {
    if (const char* l = std::getenv("PARANAV_LANG"))
        if (std::string(l) == "en") nav::setLang(nav::Lang::EN);
    std::string sd = argc > 1 ? argv[1] : "sd_card";
    std::string menuLabels = argc > 2 ? argv[2] : "/tmp/menu_labels.txt";
    World w;
    w.asp.loadOpenAir((sd + "/airspace/EXAMPLE_airspace.txt").c_str());
    w.asp.loadOpenAirText(kLongZones);
    w.asp.loadActivations((sd + "/airspace/activations.txt").c_str());
    w.wp.loadDir((sd + "/waypoints").c_str());
    w.wp.loadCupText("\"Lądowisko Zakrzewo Świerczynówek Wielkie\",LZS,PL,5206.000N,02050.000E,110.0m,3,,,,\n");
    w.route.loadCup((sd + "/route/example_route.cup").c_str());
    w.terr.setDir("/tmp");

    // ---------------- scenario 1: normal flight
    simulate(w, 560);
    w.sys.battPct = 76; w.sys.battV = 3.94f; w.sys.battMa = -112; w.sys.battMinLeft = 540; w.sys.sdOk = true;
    w.sys.sdFreeMb = 14800; w.sys.gpsModule = "u-blox MIA-M10Q"; w.sys.battSource = "BQ27220"; w.sys.recording = true;
    w.sys.logFile = "/sd/logs/2026-11-16-XPN-NAV-01.igc"; w.sys.i2cDevices = "20 51 55 5D 68 6B 77";
    {
        const NavState& n = w.fc->state();
        AirspaceDb::Query q{n.lat, n.lon, n.altMsl, n.groundMsl, w.set.qnhHpa, n.trackDeg, n.gsKmh / 3.6f, n.utcSec};
        q.dateYmd = n.dateYmd;
        w.hits = w.asp.check(q, &w.set);
    }
    render(w, "normalny");

    // ---------------- scenario 2: nothing works (no GPS, no baro, no SD, no battery)
    {
        World e;
        e.fc.reset(new FlightComputer(e.set));
        BaroSnapshot b;
        e.fc->updateNoGps(b, 5);
        e.sys.battSource = TR("brak (55:nie 6B:nie)", "none (55:no 6B:no)");
        e.sys.gpsModule = TR("brak odpowiedzi", "no response");
        e.sys.i2cDevices = TR("nic", "nothing");
        e.sys.bfState = 1;
        e.sys.loadErrors = {TR("Restart: błąd programu (panic): MENU>Pomoc>[Ekran MAPA] pomoc str.2",
                               "Restart: software error (panic): MENU>Help>[MAP screen] help p.2"),
                            TR("Raport: logs/crash-01.txt (loopTask, PC 0x4200abcd)", "Report: logs/crash-01.txt (loopTask, PC 0x4200abcd)"),
                            TR("Strefy: brak danych w bardzo_dluga_nazwa_pliku_strefy.txt",
                               "Airspace: no data in a_very_long_airspace_file_name.txt"),
                            TR("Nie mogę utworzyć pliku IGC", "Cannot create the IGC file")};
        render(e, "brak-danych");
    }

    // ---------------- scenario 3: extreme values everywhere
    {
        NavState& n = w.fc->mutableState();
        n.altMsl = 9999; n.gpsAlt = 9876; n.altStd = 9999; n.aglTerrain = 9999; n.aglTakeoff = -999;
        n.vario = n.vario1s = -12.3f; n.varioAvg = 10.8f; n.gsKmh = 199; n.trackDeg = 359;
        n.homeDistM = 987654; n.homeEteS = 99 * 3600; n.homeArrivalAgl = -99999; n.homeReachable = false;
        n.wind.valid = true; n.wind.speedKmh = 99; n.wind.fromDeg = 270; n.wind.ageS = 3600;
        n.ldCurrent = 99.9f; n.fuelLeftL = 99.9f; n.fuelUsedL = 88.88f; n.enduranceMin = 99 * 60;
        n.returnNeedL = 99.9f; n.returnMarginMin = -999; n.engineOn = false;
        n.flightTimeS = 99 * 3600 + 59 * 60; n.sats = 24; n.hdop = 99.9f; n.lat = -89.12345; n.lon = -179.12345;
        n.pressHpa = 1013.25f; n.tempC = -20; n.groundMsl = 8848;
        n.tgtValid = true; n.tgtKind = TargetKind::Route; n.routeIdx = 11; n.routeLen = 12;
        n.tgtName = "Lądowisko Zakrzewo Świerczynówek Wielkie Bardzo Długie";
        n.tgtDistM = 999000; n.tgtEteS = 99 * 3600; n.tgtReqLD = 99.9f; n.tgtArrivalAgl = -99999; n.tgtFuelArriveL = -99.9f;
        n.circling = true; n.thermalGainM = -9999; n.thermalAvg = -9.9f;
        w.set.fuelStartL = 40; w.set.fuelFlowLph = 19.9f; w.set.fuelReserveMin = 60; w.set.cruiseKmh = 90;
        w.set.qnhHpa = 1050.5f;
        w.sys.battPct = 100; w.sys.battV = 4.20f; w.sys.battMa = -1999; w.sys.battMinLeft = 99 * 60; w.sys.charging = true;
        w.sys.sdFreeMb = 999999; w.sys.airspaceZones = 9999; w.sys.airspacePoints = 999999;
        w.sys.activationEntries = 999; w.sys.activationDate = 20261116; w.sys.waypoints = 9999; w.sys.routePoints = 999;
        w.sys.terrainTiles = 999; w.sys.freeHeapKb = 999; w.sys.freePsramKb = 99999; w.sys.gpsSentences = 9999999;
        w.sys.gpsCsErr = 99999; w.sys.touchX = 959; w.sys.touchY = 539; w.sys.version = "0.1.0";
        w.sys.battSource = TR("BQ25896 (napięcie)", "BQ25896 (voltage)");
        w.sys.backlight = true;
        w.sys.bfState = 3; w.sys.bfName = "BlueFly-7970"; w.sys.bfBattPct = 100; w.sys.bfRssi = -100;
        w.sys.phoneState = 3; w.sys.phoneProgress = 0.457f;
        w.sys.phoneFile = "openaip_airspace_poland_pl_bardzo_dluga_nazwa_pliku.txt";
        w.sys.phoneLast = "812 stref – airspace/openaip_airspace_poland_pl.txt";
        n.altSource = AltSource::Wired; n.baroOk = true; n.pressHpa = 1013.25f;
        w.sys.baroBlanked = 99999; w.sys.baroSpikes = 9999;
        // hits: violation inside the long-named zone, plus vertical variants
        w.hits.clear();
        int nz = (int)w.asp.zones().size();
        AirspaceHit h;
        h.idx = nz - 2; h.insideH = true; h.vpos = VertPos::Inside; h.level = 2; h.timeToEntryS = 119;
        w.hits.push_back(h);
        h.idx = nz - 1; h.insideH = true; h.vpos = VertPos::Below; h.vertMarginM = 9999; h.level = 1;
        w.hits.push_back(h);
        for (int i = 0; i < 6; i++) {
            h.idx = i % nz; h.insideH = false; h.distM = 19999; h.timeToEntryS = 119; h.level = 1;
            w.hits.push_back(h);
        }
        // same texts as evaluateAlerts() in src/main.cpp, in the current language
        w.alerts.set(AL_AIRSPACE_IN, true, 1, TR("W STREFIE: ", "INSIDE: ") + w.asp.zones()[nz - 2].name, "R  762m AGL – FL195", 1);
        w.alerts.set(AL_AIRSPACE_NEAR, true, 1, TR("STREFA: ", "AIRSPACE: ") + w.asp.zones()[nz - 1].name,
                     TR("wlot za 1:59  (19.9 km)", "entry in 1:59  (19.9 km)"), 2);
        w.alerts.set(AL_FUEL_RETURN, true, 1, TR("WRACAJ – PALIWO!", "TURN BACK – FUEL!"),
                     TR("paliwo 99.9 l, na powrót potrzeba 99.9 l", "fuel 99.9 l, return needs 99.9 l"));
        w.alerts.set(AL_FUEL_LOW, true, 1, TR("MAŁO PALIWA", "LOW FUEL"), TR("zapas na powrót tylko -999 min", "return margin only -999 min"));
        w.alerts.set(AL_LOW_AGL, true, 1, TR("NISKO!", "LOW!"), TR("-999 m nad terenem", "-999 m AGL"));
        w.alerts.set(AL_GPS_LOST, true, 1, TR("BRAK GPS", "NO GPS"), TR("nawigacja i strefy nieaktualne", "navigation and airspace not updated"));
        w.alerts.set(AL_BATTERY, true, 1, TR("SŁABA BATERIA", "LOW BATTERY"), "1 %");
        w.alerts.set(AL_BARO_LOST, true, 1, TR("BRAK BAROMETRU", "NO BAROMETER"),
                     TR("wysokość i vario z GPS (mniej dokładne)", "altitude and vario from GPS (less accurate)"));
        w.fc->lastFlight = {true, 99 * 3600 + 59 * 60, 9999, 12.3f, -12.3f, 999.9f, 999.9f, 99.9f};
        w.sys.logFile = "/sd/logs/2026-11-16-XPN-NAV-99.igc";
        render(w, "skrajne");
        // north-up + other targets
        w.set.mapNorthUp = true;
        n.tgtKind = TargetKind::Landing;
        n.homeReachable = true; n.homeArrivalAgl = 9999; n.returnMarginMin = 99 * 60; n.engineOn = true;
        n.vario = n.vario1s = 12.3f; n.ldCurrent = NAN;
        n.altSource = AltSource::Gps; n.baroOk = false; w.sys.bfState = 2;
        render(w, "skrajne-2");
    }

    // ---------------- menu: all labels from main.cpp with worst-case values
    {
        std::ifstream f(menuLabels);
        std::string l;
        std::vector<ui::MenuItem> items;
        while (std::getline(f, l)) {
            if (l.empty()) continue;
            items.push_back(ui::MenuItem::num(l, [] { return -1060.5f; }, [](float) {}, -2000, 2000, 0.5f, 1, "km/h"));
        }
        std::printf("menu: %zu items\n", items.size());
        auto root = std::make_shared<ui::MenuItem>(ui::MenuItem::sub("root", {ui::MenuItem::sub(TR("Wysokościomierz", "Altimeter"), items)}));
        ui::Menu m;
        m.open(root);
        m.handleKey(ui::Key::NextLong);
        ui::Canvas c(fb);
        std::vector<ui::DebugBox> boxes;
        ui::Canvas::debugSink = &boxes;
        for (size_t i = 0; i < items.size(); i++) {
            boxes.clear();
            m.draw(c);
            analyse("MENU/" + items[i].label, boxes, false);
            if (i == 10) savePgm("/tmp/layout/menu.pgm");
            m.handleKey(ui::Key::NextShort);
        }
        if (items.size() > 14) {   // touch scroll column: page down from the top shows items 7..14, page up returns
            ui::Menu t;
            t.open(root);
            t.handleKey(ui::Key::NextLong);
            t.handleTap(ui::W - 30, 450);
            std::string d = t.describe();
            if (d.find("[" + items[7].label + "]") == std::string::npos) {
                std::printf("  [MENU] page down by touch failed: %s\n", d.c_str());
                problems++;
            }
            t.handleTap(ui::W - 30, 100);
            t.handleTap(ui::W - 30, 100);
            if (t.describe().find("[" + items[7].label + "]") == std::string::npos &&
                t.describe().find("[" + items[0].label + "]") == std::string::npos) {
                std::printf("  [MENU] page up by touch failed: %s\n", t.describe().c_str());
                problems++;
            }
        }
        m.handleKey(ui::Key::NextLong);  // edit
        for (size_t i = 0; i < items.size(); i++) {
            boxes.clear();
            m.draw(c);
            analyse("MENU-EDYCJA", boxes, false);
        }
        ui::Canvas::debugSink = nullptr;
    }
    // help pages: every page of every topic + missing glyphs
    {
        std::vector<ui::MenuItem> topics;
        for (const auto& t : ui::helpTopics()) {
            topics.push_back(ui::MenuItem::page(t.title, t.text));
            const char* p = t.text;
            while (*p) {
                uint32_t cp = ui::utf8Next(p);
                if (cp != 0x0A && !ui::Canvas::hasGlyph(ui::font_sb, cp)) {
                    std::printf("  [HELP/%s] glyph U+%04X missing in the font\n", t.title, (unsigned)cp);
                    problems++;
                }
            }
        }
        auto root = std::make_shared<ui::MenuItem>(ui::MenuItem::sub("root", {ui::MenuItem::sub("Pomoc", topics)}));
        ui::Menu m;
        m.open(root);
        m.handleKey(ui::Key::NextLong);
        ui::Canvas c(fb);
        std::vector<ui::DebugBox> boxes;
        ui::Canvas::debugSink = &boxes;
        int totalPages = 0;
        for (size_t i = 0; i < topics.size(); i++) {
            boxes.clear();
            m.draw(c);
            analyse(std::string("POMOC-LISTA/") + topics[i].label, boxes, false);
            m.handleKey(ui::Key::NextLong);  // open topic
            int tl = 0;
            int np = (int)ui::Menu::paginate(topics[i].text, ui::W - 50, 26, tl).size();
            for (int pg = 0; pg < np; pg++) {
                boxes.clear();
                m.draw(c);
                analyse(std::string("POMOC/") + topics[i].label + " str." + std::to_string(pg + 1), boxes, false);
                savePgm("/tmp/layout/help_" + std::to_string(i) + "_" + std::to_string(pg) + ".pgm");
                m.handleKey(ui::Key::NextShort);
                totalPages++;
            }
            m.handleKey(ui::Key::NextLong);   // back to list
            m.handleKey(ui::Key::NextShort);  // next topic
        }
        std::printf("help: %zu topics, %d pages\n", topics.size(), totalPages);
        ui::Canvas::debugSink = nullptr;
    }
    // menu fuzz: random keys/taps on a menu tree shaped like the device one (run under ASan)
    {
        using M = ui::MenuItem;
        float val = 5;
        bool flag = false;
        int closes = 0;
        auto build = [&]() {
            std::vector<M> help;
            for (const auto& t : ui::helpTopics()) help.push_back(M::page(t.title, t.text));
            std::vector<M> nums;
            for (int i = 0; i < 9; i++)
                nums.push_back(M::num(TR("Liczba ", "Number ") + std::to_string(i), [&] { return val; }, [&](float v) { val = v; }, 0,
                                      100, 0.5f, 1, "l"));
            std::vector<M> acts;
            acts.push_back(M::act(TR("Zamknij", "Close"), [] {}, true));
            acts.push_back(M::toggle(TR("Przełącz", "Toggle"), [&] { return std::string(flag ? TR("TAK", "YES") : TR("NIE", "NO")); }, [&] { flag = !flag; }));
            acts.push_back(M::sub(TR("Lista", "List"), {M::act("A", [] {}, true), M::act("B", [] {}, true)}));
            acts.push_back(M::sub(TR("Pusta", "Empty"), {}));
            return std::make_shared<M>(M::sub("root", {M::sub(TR("Akcje", "Actions"), acts), M::sub(TR("Liczby", "Numbers"), nums),
                                                       M::sub(TR("Pomoc", "Help"), help), M::act("X", [] {}, true)}));
        };
        ui::Menu m;
        m.open(build());
        ui::Canvas c(fb);
        unsigned seed = 12345;
        for (int step = 0; step < 20000; step++) {
            seed = seed * 1103515245u + 12345u;
            int r = (seed >> 16) % 10;
            if (!m.isOpen()) {
                m.open(build());
                closes++;
            }
            if (r < 8) m.handleKey((ui::Key)(1 + r % 4));
            else m.handleTap((int)((seed >> 3) % ui::W), (int)((seed >> 11) % ui::H));
            (void)m.describe();
            if (step % 7 == 0) m.draw(c);
        }
        std::printf("menu fuzz: 20000 steps, %d closes\n", closes);
    }
    // boot + power screens
    {
        ui::Canvas c(fb);
        std::vector<ui::DebugBox> boxes;
        ui::Canvas::debugSink = &boxes;
        ui::drawBoot(c, {TR("[OK] Karta SD", "[OK] SD card"),
                         TR("[!!] Barometr MS5611 nie odpowiada – sprawdź QWIIC", "[!!] Barometer MS5611 not responding – check QWIIC"),
                         "[..] I2C: 20 51 55 5D 68 6B 76 77",
                         TR("[!!] Bateria: brak (55:nie 6B:nie) 0.00 V", "[!!] Battery: none (55:no 6B:no) 0.00 V"),
                         TR("[OK] Strefy 9999, punkty 9999, trasa 999, teren 999, mapy 99",
                            "[OK] Airspace 9999, waypoints 9999, route 999, terrain 999, maps 99"),
                         TR("[!!] GPS: brak odpowiedzi", "[!!] GPS: no response")},
                     "0.1.0");
        analyse("START", boxes, false);
        boxes.clear();
        ui::drawPowerHint(c);
        analyse("WYŁĄCZANIE", boxes, false);
        boxes.clear();
        ui::drawPowerOff(c, TR("Odłącz kabel USB", "Unplug the USB cable"),
                          TR("przy zasilaniu z USB wyłączenie nie jest możliwe", "the device cannot power off while on USB power"));
        analyse("WYŁĄCZONY", boxes, false);
        ui::Canvas::debugSink = nullptr;
    }
    std::printf("problems: %d\n", problems);
    return problems ? 1 : 0;
}
