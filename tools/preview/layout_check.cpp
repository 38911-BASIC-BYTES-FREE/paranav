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
            report("poza ekranem: \"" + t.text + "\" x=" + std::to_string(t.x0) + ".." + std::to_string(t.x1) +
                   " y=" + std::to_string(t.y0) + ".." + std::to_string(t.y1));
        for (size_t j = 0; j < boxes.size(); j++) {
            if (i == j) continue;
            const auto& o = boxes[j];
            if (o.text.empty()) {
                // separator crossing (1 px tolerance)
                if (overlap(t, o, 1)) report("tekst na linii: \"" + t.text + "\"");
            } else if (j > i && overlap(t, o, 1)) {
                if (mapMode) continue;  // map labels may overlap each other in dense areas
                report("nakładanie: \"" + t.text + "\" / \"" + o.text + "\"");
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
    ui::drawUsbDisk(c, u, false);
    analyse(name + "/USB", boxes, false);
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
    ui::Canvas::debugSink = nullptr;
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
    std::string sd = argc > 1 ? argv[1] : "sd_card";
    std::string menuLabels = argc > 2 ? argv[2] : "/tmp/menu_labels.txt";
    World w;
    w.asp.loadOpenAir((sd + "/airspace/PRZYKLAD_strefy.txt").c_str());
    w.asp.loadOpenAirText(kLongZones);
    w.asp.loadActivations((sd + "/airspace/activations.txt").c_str());
    w.wp.loadDir((sd + "/waypoints").c_str());
    w.wp.loadCupText("\"Lądowisko Zakrzewo Świerczynówek Wielkie\",LZS,PL,5206.000N,02050.000E,110.0m,3,,,,\n");
    w.route.loadCup((sd + "/route/trasa.cup").c_str());
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
        e.sys.battSource = "brak (55:nie 6B:nie)";
        e.sys.gpsModule = "brak odpowiedzi";
        e.sys.i2cDevices = "nic";
        e.sys.bfState = 1;
        e.sys.loadErrors = {"Restart: błąd programu (panic): MENU>Pomoc>[Ekran MAPA] pomoc str.2",
                            "Raport: logs/crash-01.txt (loopTask, PC 0x4200abcd)",
                            "Strefy: brak danych w bardzo_dluga_nazwa_pliku_strefy.txt",
                            "Nie mogę utworzyć pliku IGC"};
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
        w.sys.battSource = "BQ25896 (napięcie)";
        w.sys.backlight = true;
        w.sys.bfState = 3; w.sys.bfName = "BlueFly-7970"; w.sys.bfBattPct = 100; w.sys.bfRssi = -100;
        w.sys.phoneState = 3; w.sys.phoneProgress = 0.457f;
        w.sys.phoneFile = "openaip_airspace_poland_pl_bardzo_dluga_nazwa_pliku.txt";
        w.sys.phoneLast = "812 stref – airspace/openaip_airspace_poland_pl.txt";
        n.altSource = AltSource::BlueFly; n.baroOk = true;
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
        w.alerts.set(AL_AIRSPACE_IN, true, 1, "W STREFIE: " + w.asp.zones()[nz - 2].name, "R  762m AGL – FL195", 1);
        w.alerts.set(AL_AIRSPACE_NEAR, true, 1, "STREFA: " + w.asp.zones()[nz - 1].name, "wlot za 1:59  (19.9 km)", 2);
        w.alerts.set(AL_FUEL_RETURN, true, 1, "WRACAJ – PALIWO!", "paliwo 99.9 l, na powrót potrzeba 99.9 l");
        w.alerts.set(AL_FUEL_LOW, true, 1, "MAŁO PALIWA", "zapas na powrót tylko -999 min");
        w.alerts.set(AL_LOW_AGL, true, 1, "NISKO!", "-999 m nad terenem");
        w.alerts.set(AL_GPS_LOST, true, 1, "BRAK GPS", "nawigacja i strefy nieaktualne");
        w.alerts.set(AL_BATTERY, true, 1, "SŁABA BATERIA", "1 %");
        w.alerts.set(AL_BARO_LOST, true, 1, "BRAK BAROMETRU", "wysokość i vario z GPS (mniej dokładne)");
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
        std::printf("menu: %zu pozycji\n", items.size());
        auto root = std::make_shared<ui::MenuItem>(ui::MenuItem::sub("root", {ui::MenuItem::sub("Wysokościomierz", items)}));
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
            m.handleKey(ui::Key::NextShort);
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
                    std::printf("  [POMOC/%s] brak znaku U+%04X w foncie\n", t.title, (unsigned)cp);
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
        std::printf("pomoc: %zu tematów, %d stron\n", topics.size(), totalPages);
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
                nums.push_back(M::num("Liczba " + std::to_string(i), [&] { return val; }, [&](float v) { val = v; }, 0,
                                      100, 0.5f, 1, "l"));
            std::vector<M> acts;
            acts.push_back(M::act("Zamknij", [] {}, true));
            acts.push_back(M::toggle("Przełącz", [&] { return std::string(flag ? "TAK" : "NIE"); }, [&] { flag = !flag; }));
            acts.push_back(M::sub("Lista", {M::act("A", [] {}, true), M::act("B", [] {}, true)}));
            acts.push_back(M::sub("Pusta", {}));
            return std::make_shared<M>(M::sub("root", {M::sub("Akcje", acts), M::sub("Liczby", nums),
                                                       M::sub("Pomoc", help), M::act("X", [] {}, true)}));
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
        std::printf("menu fuzz: 20000 kroków, %d zamknięć\n", closes);
    }
    // boot + power screens
    {
        ui::Canvas c(fb);
        std::vector<ui::DebugBox> boxes;
        ui::Canvas::debugSink = &boxes;
        ui::drawBoot(c, {"[OK] Karta SD", "[!!] Barometr MS5611 nie odpowiada – sprawdź QWIIC",
                         "[..] I2C: 20 51 55 5D 68 6B 76 77", "[!!] Bateria: brak: 0x55 milczy, 0x6B milczy 0.00 V",
                         "[OK] Strefy 9999, punkty 9999, trasa 999, teren 999 kafli", "[!!] GPS: brak odpowiedzi"},
                     "0.1.0");
        analyse("START", boxes, false);
        boxes.clear();
        ui::drawPowerHint(c);
        analyse("WYŁĄCZANIE", boxes, false);
        boxes.clear();
        ui::drawPowerOff(c, "Odłącz kabel USB", "przy zasilaniu z USB wyłączenie nie jest możliwe");
        analyse("WYŁĄCZONY", boxes, false);
        ui::Canvas::debugSink = nullptr;
    }
    std::printf("problemów: %d\n", problems);
    return problems ? 1 : 0;
}
