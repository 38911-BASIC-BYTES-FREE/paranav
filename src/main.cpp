// ParaNav - paramotor flight computer for LilyGO T5 E-Paper S3 Pro + MS5611
//
// Tasks:
//   sensorTask (core 1, prio 5): MS5611 @ ~90 Hz -> Kalman vario, GNSS NMEA parsing
//   inputTask  (core 1, prio 3): buttons + touch -> event queue           (board.cpp)
//   loop()     (Arduino task)  : 1 Hz flight logic, airspace, alerts, logger, UI rendering
#include <Arduino.h>
#include <SD.h>
#include <Wire.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_core_dump.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <strings.h>
#include <mutex>

#include "airspace.h"
#include "alerts.h"
#include "board.h"
#include "config.h"
#include "flight.h"
#include "gps.h"
#include "logger.h"
#include "ms5611.h"
#include "bluefly_ble.h"
#include "phone_ble.h"
#include "settings.h"
#include "terrain.h"
#include "topo.h"
#include "usb_disk.h"
#include "ui/canvas.h"
#include "ui/help.h"
#include "ui/menu.h"
#include "ui/screens.h"
#include "waypoints.h"
#include "yield.h"

using namespace nav;

// Arduino loop task stack (default 8 KB is too small for menu building + rendering + file parsing)
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

// ------------------------------------------------------------------ crash breadcrumbs
// Kept in RTC memory that survives a software reset/panic: after a crash the boot screen shows
// what the device was doing just before it.
RTC_NOINIT_ATTR static char gCrumb[112];
RTC_NOINIT_ATTR static uint32_t gCrumbMagic;
static constexpr uint32_t kCrumbMagic = 0x50415241;  // "PARA"
static void crumb(const std::string& s) {
    strncpy(gCrumb, s.c_str(), sizeof gCrumb - 1);
    gCrumb[sizeof gCrumb - 1] = 0;
    gCrumbMagic = kCrumbMagic;
}

// ------------------------------------------------------------------ global state
static Settings settings;
static AirspaceDb airspace;
static WaypointDb waypoints;
static Route route;
static Terrain terrain;
static TopoMap topo;          // background map tiles (/sd/maps)
static TopoView topoView;     // TOPO screen view centre/zoom between frames
static bool usbMode = false;  // SD card handed over to the PC as a USB drive
static FlightComputer fc(settings);
static AlertManager alerts;
static TrackLogger logger;
static ui::SysInfo sys;
static std::vector<AirspaceHit> hits;
static ui::Menu menu;
static ui::ScreenId screen = ui::SCR_FLIGHT;
static bool showSummary = false;
static void shutdownDevice();
static bool powerArming = false;
static bool backlightOn = false;
static uint32_t backlightSince = 0;

static void setBacklight(bool on) {
    backlightOn = on;
    backlightSince = millis();
    board::backlight(on ? settings.backlightPct : 0);
    sys.backlight = on;
}
static std::vector<std::string> routeFiles;
static int routeFileIdx = 0;

static MS5611 baro;
static GpsModule gps;

// shared between sensor task and loop()
static std::mutex shMtx;
static BaroSnapshot shBaro;
static GpsFix shFix;
static bool shNewFix = false;

// ------------------------------------------------------------------ sensor task
// Altitude source is chosen automatically, sample by sample:
//   1. MS5611 on the QWIIC cable, if it answers (re-probed every 10 s, so it can be plugged in later),
//   2. otherwise BlueFlyVario over Bluetooth, if it sent data during the last second,
//   3. otherwise no barometer - FlightComputer then uses GPS altitude (and a GPS-based vario).
static void sensorTask(void*) {
    KalmanVario kv(0.6f, 0.25f);
    AltSource active = AltSource::None;
    uint32_t lastUs = micros(), lastProbe = 0, lastBfMs = 0;
    float bfPress = 0, bfAlt = 0;
    bool bfNew = false, needReset = true;
    auto feed = [&](float h, uint32_t nowUs) {
        if (needReset) {
            kv.reset(h);  // switching sensors: restart the filter (no false climb/sink spike)
            needReset = false;
        } else {
            kv.update(h, (nowUs - lastUs) * 1e-6f);
        }
        lastUs = nowUs;
    };
    for (;;) {
        uint32_t nowMs = millis();
        // --- wired MS5611 (hot-plug)
        if (!baro.ok() && nowMs - lastProbe > 10000) {
            lastProbe = nowMs;
            baro.begin(Wire);
        }
        bool wiredNew = baro.ok() && baro.poll();
        bool wiredOk = baro.ok();
        // --- BlueFly samples (drain the queue, keep the newest; feed all to Kalman if BlueFly is active)
        bluefly::Sample bs;
        while (bluefly::getSample(bs)) {
            float h = bs.kind == nav::BlueFlyParser::Kind::Pressure ? baroAltitude(bs.value, 1013.25f) : bs.value;
            bfPress = bs.kind == nav::BlueFlyParser::Kind::Pressure
                          ? bs.value
                          : 1013.25f * std::pow(1.0f - h / 44330.77f, 5.25588f);
            bfAlt = h;
            lastBfMs = millis();
            bfNew = true;
            if (active == AltSource::BlueFly) feed(h, bs.micros);
        }
        bool bfOk = lastBfMs && millis() - lastBfMs < 1000;

        AltSource want = wiredOk ? AltSource::Wired : (bfOk ? AltSource::BlueFly : AltSource::None);
        if (want != active) {
            active = want;
            needReset = true;
            if (active == AltSource::BlueFly) feed(bfAlt, micros());  // last BlueFly value as a start point
        }
        if (active == AltSource::Wired && wiredNew) feed(baroAltitude(baro.pressureHpa(), 1013.25f), micros());
        if ((active == AltSource::Wired && wiredNew) || (active == AltSource::BlueFly && bfNew) ||
            active == AltSource::None) {
            std::lock_guard<std::mutex> l(shMtx);
            shBaro.ok = active != AltSource::None && !needReset;
            shBaro.source = active;
            if (active == AltSource::Wired) {
                shBaro.pressHpa = baro.pressureHpa();
                shBaro.tempC = baro.temperatureC();
            } else if (active == AltSource::BlueFly) {
                shBaro.pressHpa = bfPress;
                shBaro.tempC = NAN;
            }
            shBaro.altStd = kv.altitude();
            shBaro.vario = kv.vario();
        }
        bfNew = false;
        if (gps.poll()) {
            std::lock_guard<std::mutex> l(shMtx);
            shFix = gps.fix();
            shNewFix = true;
        }
        vTaskDelay(1);
    }
}

// ------------------------------------------------------------------ data loading
static bool endsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    if (s.size() < n) return false;
    return strcasecmp(s.c_str() + s.size() - n, suf) == 0;
}

static void loadDataImpl();
static void loadData() {
    try {
        loadDataImpl();
    } catch (const std::bad_alloc&) {
        // never reboot because of too much data on the card - keep what fits and report it
        sys.loadErrors.push_back("Za mało pamięci na wszystkie dane z karty SD");
        Serial.println("[data] out of memory while loading");
    }
    sys.airspaceZones = (int)airspace.zones().size();
    sys.airspacePoints = (int)airspace.pointCount();
    sys.waypoints = (int)waypoints.all().size();
    Serial.printf("[data] zones %d (%d pts), wpts %d, internal free %u B, PSRAM free %u B\n", sys.airspaceZones,
                  sys.airspacePoints, sys.waypoints, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void loadDataImpl() {
    sys.loadErrors.clear();
    hits.clear();  // hit indices refer to the old zone list
    alerts.set(AL_AIRSPACE_IN, false, 0);
    alerts.set(AL_AIRSPACE_NEAR, false, 0);
    airspace.clear();
    DIR* d = opendir(PATH_AIRSPACE);
    if (d) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n[0] == '.' || !endsWith(n, ".txt") || strcasecmp(n.c_str(), "activations.txt") == 0) continue;
            std::string p = std::string(PATH_AIRSPACE) + "/" + n;
            int k = airspace.loadOpenAir(p.c_str());
            if (k <= 0) sys.loadErrors.push_back("Strefy: brak danych w " + n);
        }
        closedir(d);
    }
    sys.activationEntries = airspace.loadActivations(PATH_ACTIVATIONS);
    sys.activationDate = airspace.activationDate();
    sys.airspaceZones = (int)airspace.zones().size();
    sys.airspacePoints = (int)airspace.pointCount();

    waypoints.clear();
    waypoints.loadDir(PATH_WAYPOINTS);
    sys.waypoints = (int)waypoints.all().size();

    routeFiles.clear();
    d = opendir(PATH_ROUTE);
    if (d) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n[0] != '.' && endsWith(n, ".cup")) routeFiles.push_back(std::string(PATH_ROUTE) + "/" + n);
        }
        closedir(d);
    }
    std::sort(routeFiles.begin(), routeFiles.end());
    route = Route{};
    if (!routeFiles.empty()) route.loadCup(routeFiles[routeFileIdx % routeFiles.size()].c_str());
    sys.routePoints = (int)route.pts.size();

    terrain.setDir(PATH_TERRAIN);
    sys.terrainTiles = terrain.countTiles();

    topo.setDir(PATH_MAPS);
    sys.topoAreas = topo.scan();
    if (topo.errors()) sys.loadErrors.push_back("Mapy: " + topo.lastError());
    topoView.valid = false;
}

static void saveSettings() {
    if (sys.sdOk) settings.save(PATH_CONFIG);
}

// ------------------------------------------------------------------ USB drive mode
static void showMessage(const char* l1, const char* l2, uint32_t ms) {
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    ui::drawPowerOff(c, l1, l2);
    board::displayUpdate(false);
    delay(ms);
}

// Hands the SD card over to the PC. All file access stops first (IGC log closed, map/terrain
// files closed, phone uploads off); sys.sdOk = false blocks settings saves while in this mode.
static void usbEnter() {
    if (!usbdisk::supported()) {
        showMessage("Tryb USB niedostępny", usbdisk::lastError(), 2500);
        return;
    }
    if (fc.state().phase == Phase::Flying) {
        showMessage("Tryb USB niedostępny w locie", "wyląduj i zakończ lot", 2500);
        return;
    }
    if (!sys.sdOk) {
        showMessage("Brak karty SD", "włóż kartę i uruchom ponownie", 2500);
        return;
    }
    if (sys.phoneState == 3) {
        showMessage("Trwa odbiór z telefonu", "poczekaj na koniec przesyłania", 2500);
        return;
    }
    crumb("tryb USB: start");
    logger.stop();
    sys.recording = false;
    saveSettings();
    phone::setEnabled(false);
    topo.close();
    terrain.setDir(PATH_TERRAIN);  // closes open .hgt files
    sys.sdOk = false;
    if (!usbdisk::start()) {
        sys.sdOk = true;
        phone::setEnabled(settings.phoneEnabled);
        showMessage("Tryb USB: błąd", usbdisk::lastError(), 2500);
        return;
    }
    usbMode = true;
    sys.usbDisk = true;
}

// Takes the card back: remount (the PC changed the FAT) and reload everything.
static void usbExit() {
    crumb("tryb USB: koniec, ponowne wczytanie");
    usbdisk::stop();
    usbMode = false;
    sys.usbDisk = false;
    SD.end();
    sys.sdOk = board::sdInit();
    if (sys.sdOk) {
        settings.load(PATH_CONFIG);
        loadData();
        sys.sdFreeMb = board::sdFreeMb();
    } else {
        sys.loadErrors.push_back("Karta SD nie odpowiada po trybie USB");
    }
    phone::setEnabled(settings.phoneEnabled);
}

// ------------------------------------------------------------------ menu
static std::string onOff(bool b) { return b ? "TAK" : "NIE"; }

__attribute__((noinline)) static ui::MenuItem menuLot() {
    using M = ui::MenuItem;
    std::vector<M> lot;
    lot.push_back(M::act("Ustaw START tutaj", [] { fc.setHomeHere(); }, true));
    lot.push_back(M::toggle("Silnik pracuje", [] { return onOff(fc.state().engineOn); },
                  [] { fc.setEngine(!fc.state().engineOn); }));
    lot.push_back(M::act("Zakończ lot (zamknij IGC)",
               [] {
                   logger.stop();
                   sys.recording = false;
                   fc.resetFlight();
               },
               true));
    return M::sub("Lot", std::move(lot));
}

__attribute__((noinline)) static ui::MenuItem menuFuel() {
    using M = ui::MenuItem;
    std::vector<M> fuel;
    fuel.push_back(M::num("Paliwo na starcie", [] { return settings.fuelStartL; }, [](float v) { settings.fuelStartL = v; }, 0, 40,
               0.5f, 1, "l"));
    fuel.push_back(M::num("Zużycie", [] { return settings.fuelFlowLph; }, [](float v) { settings.fuelFlowLph = v; }, 0.5f, 20, 0.1f,
               1, "l/h"));
    fuel.push_back(M::num("Rezerwa", [] { return settings.fuelReserveMin; }, [](float v) { settings.fuelReserveMin = v; }, 0, 60,
               5, 0, "min"));
    fuel.push_back(M::num("Prędkość przelotowa", [] { return settings.cruiseKmh; }, [](float v) { settings.cruiseKmh = v; }, 15,
               90, 1, 0, "km/h"));
    fuel.push_back(M::act("Zatankowano do pełna", [] { fc.refuel(settings.fuelStartL); }, true));
    return M::sub("Paliwo", std::move(fuel));
}

__attribute__((noinline)) static ui::MenuItem menuAlti() {
    using M = ui::MenuItem;
    std::vector<M> alti;
    alti.push_back(M::num("QNH", [] { return settings.qnhHpa; }, [](float v) { fc.setQnh(v); }, 950, 1060, 0.5f, 1, "hPa"));
    alti.push_back(M::num("Wysokość teraz", [] { return std::isnan(fc.state().altMsl) ? 0.f : fc.state().altMsl; },
               [](float v) { fc.setAltitude(v); }, -100, 5000, 1, 0, "m"));
    alti.push_back(M::toggle("Auto QNH (GPS/teren)", [] { return onOff(settings.autoQnh); },
                  [] { settings.autoQnh = !settings.autoQnh; }));
    alti.push_back(M::act("Kalibruj ponownie z GPS",
               [] {
                   settings.autoQnh = true;
                   fc.mutableState().qnhCalibrated = false;
               },
               true));
    return M::sub("Wysokościomierz", std::move(alti));
}

__attribute__((noinline)) static ui::MenuItem menuNav() {
    using M = ui::MenuItem;
    std::vector<M> navm;
    navm.push_back(M::act("Cel: START (Go Home)", [] { fc.setTargetHome(); screen = ui::SCR_NAV; }, true));
    navm.push_back(M::act("Cel: najbliższe lądowisko", [] { fc.setTargetNearestLanding(); screen = ui::SCR_NAV; }, true));
    if (!route.pts.empty()) {
        navm.push_back(M::act("Trasa " + route.name + ": start", [] { fc.setTargetRoute(0); screen = ui::SCR_NAV; }, true));
        navm.push_back(M::act("Trasa: następny punkt", [] { fc.routeNext(); }, true));
        navm.push_back(M::act("Trasa: poprzedni punkt", [] { fc.routePrev(); }, true));
    }
    if (routeFiles.size() > 1)
        navm.push_back(M::act("Zmień plik trasy", [] {
            routeFileIdx = (routeFileIdx + 1) % (int)routeFiles.size();
            route.loadCup(routeFiles[routeFileIdx].c_str());
            sys.routePoints = (int)route.pts.size();
        }, true));
    // waypoint list sorted by distance (max 30)
    {
        std::vector<std::pair<float, int>> d;
        const NavState& n = fc.state();
        for (size_t i = 0; i < waypoints.all().size(); i++) {
            const auto& w = waypoints.all()[i];
            float dist = n.fix ? (float)distanceM({n.lat, n.lon}, {w.lat, w.lon}) : 0.f;
            d.push_back({dist, (int)i});
        }
        std::sort(d.begin(), d.end());
        std::vector<M> list;
        for (size_t k = 0; k < d.size() && k < 30; k++) {
            int idx = d[k].second;
            char lbl[64];
            snprintf(lbl, sizeof lbl, "%s  (%.1f km)", waypoints.all()[idx].name.c_str(), d[k].first / 1000.f);
            list.push_back(M::act(lbl, [idx] { fc.setTargetWaypoint(idx); screen = ui::SCR_NAV; }, true));
        }
        if (!list.empty()) navm.push_back(M::sub("Cel: punkt z listy", list));
    }
    return M::sub("Nawigacja", std::move(navm));
}

__attribute__((noinline)) static ui::MenuItem menuGlide() {
    using M = ui::MenuItem;
    std::vector<M> glide;
    glide.push_back(M::num("Doskonałość (bez silnika)", [] { return settings.glideRatio; }, [](float v) { settings.glideRatio = v; },
               2, 12, 0.1f, 1, ""));
    glide.push_back(M::num("Prędkość ślizgu", [] { return settings.glideSpeedKmh; }, [](float v) { settings.glideSpeedKmh = v; }, 20,
               60, 1, 0, "km/h"));
    glide.push_back(M::num("Zapas wysokości", [] { return settings.glideMarginM; }, [](float v) { settings.glideMarginM = v; }, 0,
               500, 10, 0, "m"));
    return M::sub("Szybowanie", std::move(glide));
}

__attribute__((noinline)) static ui::MenuItem menuAlarms() {
    using M = ui::MenuItem;
    std::vector<M> alarms;
    alarms.push_back(M::num("Strefa: ostrzeż przed", [] { return settings.asHorizWarnS; }, [](float v) { settings.asHorizWarnS = v; },
               10, 300, 10, 0, "s"));
    alarms.push_back(M::num("Strefa: odległość", [] { return settings.asHorizWarnM; }, [](float v) { settings.asHorizWarnM = v; }, 100,
               3000, 100, 0, "m"));
    alarms.push_back(M::num("Strefa: bufor pionowy", [] { return settings.asVertWarnM; }, [](float v) { settings.asVertWarnM = v; }, 0,
               500, 10, 0, "m"));
    alarms.push_back(M::num("Niska wysokość (0=wył.)", [] { return settings.lowAglWarnM; }, [](float v) { settings.lowAglWarnM = v; },
               0, 500, 10, 0, "m"));
    alarms.push_back(M::num("Bateria", [] { return (float)settings.battWarnPct; }, [](float v) { settings.battWarnPct = (int)v; }, 0,
               50, 5, 0, "%"));
    return M::sub("Alarmy", std::move(alarms));
}

__attribute__((noinline)) static ui::MenuItem menuDisp() {
    using M = ui::MenuItem;
    std::vector<M> disp;
    disp.push_back(M::toggle("Mapa: północ u góry", [] { return onOff(settings.mapNorthUp); },
                  [] { settings.mapNorthUp = !settings.mapNorthUp; }));
    disp.push_back(M::num("Mapa: skala (0-5)", [] { return (float)settings.mapScaleIdx; },
               [](float v) { settings.mapScaleIdx = (int)v; }, 0, 5, 1, 0, ""));
    disp.push_back(M::num("TOPO: skala (10-14)", [] { return (float)settings.topoZoom; },
               [](float v) { settings.topoZoom = (int)v; }, 10, 14, 1, 0, ""));
    disp.push_back(M::toggle("Obróć ekran o 180°", [] { return onOff(settings.flip180); },
                  [] {
                      settings.flip180 = !settings.flip180;
                      board::inputSetFlip(settings.flip180);
                  }));
    disp.push_back(M::num("Pełne odświeżanie co", [] { return (float)settings.fullRefreshS; },
               [](float v) { settings.fullRefreshS = (int)v; }, 0, 600, 30, 0, "s"));
    disp.push_back(M::num("Strefa czasowa (UTC+)", [] { return settings.tzOffsetMin / 60.f; },
               [](float v) { settings.tzOffsetMin = (int)(v * 60); }, -12, 14, 1, 0, "h"));
    disp.push_back(M::toggle("Podświetlenie", [] { return onOff(backlightOn); }, [] { setBacklight(!backlightOn); }));
    disp.push_back(M::num("Jasność podświetlenia", [] { return (float)settings.backlightPct; },
                          [](float v) {
                              settings.backlightPct = (int)v;
                              if (backlightOn) board::backlight(settings.backlightPct);
                          },
                          5, 100, 5, 0, "%"));
    disp.push_back(M::num("Podświetl. wyłącz po (0=nigdy)", [] { return (float)settings.backlightOffMin; },
                          [](float v) { settings.backlightOffMin = (int)v; }, 0, 60, 1, 0, "min"));
    return M::sub("Ekran i mapa", std::move(disp));
}

__attribute__((noinline)) static ui::MenuItem menuSys() {
    using M = ui::MenuItem;
    std::vector<M> sysm;
    sysm.push_back(M::toggle("BlueFly (Bluetooth)", [] { return onOff(settings.blueflyEnabled); },
                             [] {
                                 settings.blueflyEnabled = !settings.blueflyEnabled;
                                 bluefly::setEnabled(settings.blueflyEnabled);
                                 if (settings.blueflyEnabled) bluefly::retryStack();
                             }));
    sysm.push_back(M::act("BlueFly: połącz ponownie", [] { bluefly::reconnect(); }, true));
    sysm.push_back(M::toggle("Telefon (Bluetooth)", [] { return onOff(settings.phoneEnabled); },
                             [] {
                                 settings.phoneEnabled = !settings.phoneEnabled;
                                 phone::setEnabled(settings.phoneEnabled);
                                 if (settings.phoneEnabled) bluefly::retryStack();
                             }));
    sysm.push_back(M::act("Wczytaj ponownie dane z SD", [] { loadData(); }, true));
    sysm.push_back(M::act("Tryb USB – karta SD jako dysk", [] { usbEnter(); }, true));
    sysm.push_back(M::num("VCOM e-papieru (restart)", [] { return (float)settings.vcomMv; },
               [](float v) { settings.vcomMv = (int)v; }, 1000, 3000, 10, 0, "mV"));
    return M::sub("System", std::move(sysm));
}

__attribute__((noinline)) static ui::MenuItem menuOff() {
    using M = ui::MenuItem;
    std::vector<M> off;
    off.push_back(M::act("TAK – zapisz lot i wyłącz", [] { shutdownDevice(); }, true));
    return M::sub("Wyłącz urządzenie", std::move(off));
}

__attribute__((noinline)) static ui::MenuItem menuHelp() {
    std::vector<ui::MenuItem> help;
    help.reserve(ui::helpTopics().size());
    for (const auto& t : ui::helpTopics()) help.push_back(ui::MenuItem::page(t.title, t.text));
    return ui::MenuItem::sub("Pomoc", std::move(help));
}

// The menu is built on demand. Every sub-menu has its own (non-inlined) function so that the
// temporaries do not pile up in one huge stack frame - one big function needed >7 KB of stack
// and overflowed the 8 KB Arduino loop task (device reset on opening the menu).
static std::shared_ptr<ui::MenuItem> buildMenu() {
    using M = ui::MenuItem;
    auto root = std::make_shared<M>();
    root->kind = M::Submenu;
    root->label = "root";
    root->children.reserve(10);
    root->children.push_back(menuLot());
    root->children.push_back(menuFuel());
    root->children.push_back(menuAlti());
    root->children.push_back(menuNav());
    root->children.push_back(menuGlide());
    root->children.push_back(menuAlarms());
    root->children.push_back(menuDisp());
    root->children.push_back(menuSys());
    root->children.push_back(menuHelp());
    root->children.push_back(menuOff());
    return root;
}

// ------------------------------------------------------------------ alerts
static void evaluateAlerts() {
    const NavState& n = fc.state();
    uint32_t now = n.uptimeS;
    bool flying = n.phase == Phase::Flying;
    char b[96];

    const AirspaceHit* in = nullptr;
    const AirspaceHit* near = nullptr;
    for (auto& h : hits) {
        if (h.idx < 0 || h.idx >= (int)airspace.zones().size()) continue;
        if (h.level == 2 && !in) in = &h;
        if (h.level == 1 && !near) near = &h;
    }
    if (in) {
        const Airspace& a = airspace.zones()[in->idx];
        snprintf(b, sizeof b, "%s  %s – %s", a.cls.c_str(), a.lower.label().c_str(), a.upper.label().c_str());
        alerts.set(AL_AIRSPACE_IN, flying, now, "W STREFIE: " + a.name, b, in->idx);
    } else {
        alerts.set(AL_AIRSPACE_IN, false, now);
    }
    if (near) {
        const Airspace& a = airspace.zones()[near->idx];
        if (near->insideH)
            snprintf(b, sizeof b, "%s %d m", near->vpos == VertPos::Below ? "do podstawy" : "do pułapu",
                     (int)near->vertMarginM);
        else if (near->timeToEntryS >= 0)
            snprintf(b, sizeof b, "wlot za %s  (%.1f km)", ui::fmtDur(near->timeToEntryS, false).c_str(),
                     near->distM / 1000.f);
        else
            snprintf(b, sizeof b, "odległość %.1f km", near->distM / 1000.f);
        alerts.set(AL_AIRSPACE_NEAR, flying, now, "STREFA: " + a.name, b, near->idx);
    } else {
        alerts.set(AL_AIRSPACE_NEAR, false, now);
    }
    bool fuelRet = flying && n.homeSet && n.fuelConfigured && n.returnMarginMin < 0;
    snprintf(b, sizeof b, "paliwo %.1f l, na powrót potrzeba %.1f l", n.fuelLeftL, n.returnNeedL);
    alerts.set(AL_FUEL_RETURN, fuelRet, now, "WRACAJ – PALIWO!", b);
    bool fuelLow = flying && n.homeSet && n.fuelConfigured && n.returnMarginMin >= 0 && n.returnMarginMin < 10;
    snprintf(b, sizeof b, "zapas na powrót tylko %d min", (int)n.returnMarginMin);
    alerts.set(AL_FUEL_LOW, fuelLow, now, "MAŁO PALIWA", b);
    bool low = flying && settings.lowAglWarnM > 0 && !std::isnan(n.aglTerrain) && n.aglTerrain < settings.lowAglWarnM;
    snprintf(b, sizeof b, "%d m nad terenem", (int)n.aglTerrain);
    alerts.set(AL_LOW_AGL, low, now, "NISKO!", b);
    alerts.set(AL_GPS_LOST, flying && n.fixAgeS > 5, now, "BRAK GPS", "nawigacja i strefy nieaktualne");
    // barometer lost during the flight (cable unplugged / BlueFly out of range)
    static bool hadBaro = false;
    if (n.baroOk) hadBaro = true;
    if (!flying) hadBaro = n.baroOk;
    alerts.set(AL_BARO_LOST, flying && hadBaro && !n.baroOk, now, "BRAK BAROMETRU",
               "wysokość i vario z GPS (mniej dokładne)");
    snprintf(b, sizeof b, "%d %%", sys.battPct);
    alerts.set(AL_BATTERY, sys.battPct >= 0 && sys.battPct < settings.battWarnPct && !sys.charging, now,
               "SŁABA BATERIA", b);
}

// ------------------------------------------------------------------ 1 Hz cycle
static void onSecond(bool haveFix, const GpsFix& g, const BaroSnapshot& b) {
    crumb(std::string("obliczenia 1 Hz") + (menu.isOpen() ? " (w menu)" : ""));
    uint32_t up = millis() / 1000;
    if (haveFix) fc.update(g, b, up);
    else fc.updateNoGps(b, up);
    const NavState& n = fc.state();
    {
        int ai = n.fix ? topo.areaFor(n.lat, n.lon) : -1;
        sys.topoArea = ai >= 0 ? topo.areas()[ai].title : "";
    }

    // airspace
    if (n.fix && !airspace.zones().empty()) {
        AirspaceDb::Query q{n.lat, n.lon, n.altMsl, n.groundMsl, settings.qnhHpa, n.trackDeg, n.gsKmh / 3.6f, n.utcSec};
        q.horizWarnS = settings.asHorizWarnS;
        q.horizWarnM = settings.asHorizWarnM;
        q.vertWarnM = settings.asVertWarnM;
        q.dateYmd = n.dateYmd;
        hits = airspace.check(q, &settings);
    } else if (!n.fix) {
        // keep last hits but they are stale; alerts handle GPS loss
    }

    // logger
    if (fc.takeoffEvent && sys.sdOk && n.timeValid) {
        LogPoint p{n.year, n.month, n.day, n.hour, n.minute, n.second, n.lat, n.lon, n.fix, n.altStd, n.gpsAlt, n.altMsl};
        if (logger.start(PATH_LOGS, p, settings.pilot, settings.glider, settings.gliderId, gps.moduleName(),
                         PARANAV_VERSION)) {
            sys.recording = true;
            sys.logFile = logger.igcPath();
        } else {
            sys.loadErrors.push_back("Nie mogę utworzyć pliku IGC");
        }
    }
    if (logger.active() && haveFix && n.timeValid) {
        LogPoint p{n.year, n.month, n.day, n.hour, n.minute, n.second, n.lat, n.lon, n.fix, n.altStd, n.gpsAlt, n.altMsl};
        logger.add(p);
    }
    if (fc.landingEvent) {
        logger.stop();
        sys.recording = false;
        showSummary = true;
        saveSettings();
    }
    evaluateAlerts();
}

// ------------------------------------------------------------------ input
static void cycleTarget() {
    switch (fc.state().tgtKind) {
        case TargetKind::Home:
            if (!route.pts.empty()) fc.setTargetRoute(route.active);
            else fc.setTargetNearestLanding();
            break;
        case TargetKind::Route: fc.setTargetNearestLanding(); break;
        default: fc.setTargetHome(); break;
    }
}

static const char* evName(board::Ev t) {
    using board::Ev;
    switch (t) {
        case Ev::KeyShort: return "KLAWISZ krótko";
        case Ev::KeyLong: return "KLAWISZ długo";
        case Ev::BootShort: return "BOOT krótko";
        case Ev::BootLong: return "BOOT długo";
        case Ev::Tap: return "dotyk";
        case Ev::BothShort: return "oba krótko";
        case Ev::PowerArm:
        case Ev::PowerCancel:
        case Ev::PowerOff: return "oba długo";
        default: return "?";
    }
}

static bool handleEventImpl(const board::Event& e);
static bool handleEvent(const board::Event& e) {
    std::string where = menu.isOpen() ? menu.describe() : std::string("ekran ") + ui::screenName(screen);
    crumb(where + ": " + evName(e.type));
    bool r = handleEventImpl(e);
    crumb(std::string("po: ") + where + ": " + evName(e.type));
    return r;
}

static bool handleEventImpl(const board::Event& e) {
    using board::Ev;
    // two-button power-off works everywhere (menu, summary, alerts)
    if (e.type == Ev::PowerArm) { powerArming = true; return true; }
    if (e.type == Ev::PowerCancel) { powerArming = false; return true; }
    if (e.type == Ev::BothShort) {  // both buttons tapped together: front light on/off
        setBacklight(!backlightOn);
        return true;
    }
    if (e.type == Ev::PowerOff) {
        powerArming = false;
        shutdownDevice();
        return true;
    }
    if (usbMode) {  // only "leave USB mode" here
        if (e.type == Ev::KeyLong || e.type == Ev::BootLong) usbExit();
        return true;
    }
    if (showSummary) {
        showSummary = false;
        return true;
    }
    if (menu.isOpen()) {
        switch (e.type) {
            case Ev::KeyShort: menu.handleKey(ui::Key::NextShort); break;
            case Ev::KeyLong: menu.handleKey(ui::Key::NextLong); break;
            case Ev::BootShort: menu.handleKey(ui::Key::PrevShort); break;
            case Ev::BootLong: menu.handleKey(ui::Key::PrevLong); break;
            case Ev::Tap: menu.handleTap(e.x, e.y); break;
            default: break;
        }
        if (menu.consumeDirty()) saveSettings();
        return true;
    }
    int top = alerts.top();
    if (top >= 0 && (e.type == Ev::KeyShort || e.type == Ev::BootShort || (e.type == Ev::Tap && e.y < 160))) {
        alerts.ack(top, fc.state().uptimeS);
        return true;
    }
    switch (e.type) {
        case Ev::KeyShort: screen = (ui::ScreenId)((screen + 1) % ui::SCR_COUNT); break;
        case Ev::BootShort: screen = (ui::ScreenId)((screen + ui::SCR_COUNT - 1) % ui::SCR_COUNT); break;
        case Ev::KeyLong: menu.open(buildMenu()); break;
        case Ev::BootLong:
            fc.setTargetHome();
            screen = ui::SCR_NAV;
            break;
        case Ev::Tap: {
            if (e.y < 40) { menu.open(buildMenu()); break; }
            if (screen == ui::SCR_FUEL) {
                ui::FuelButton fb = ui::fuelButtonAt(e.x, e.y);
                if (fb != ui::FB_NONE) {
                    if (fb == ui::FB_MINUS) settings.fuelStartL = std::max(0.f, settings.fuelStartL - 0.5f);
                    if (fb == ui::FB_PLUS) settings.fuelStartL += 0.5f;
                    if (fb == ui::FB_FULL) fc.refuel(settings.fuelStartL);
                    if (fb == ui::FB_ENGINE) fc.setEngine(!fc.state().engineOn);
                    saveSettings();
                    break;
                }
            }
            if (screen == ui::SCR_TOPO) {  // upper half = zoom in, lower half = zoom out
                int dz = ui::topoZoomTapAt(e.x, e.y);
                if (dz) {
                    settings.topoZoom = std::max(kTopoMinZoom, std::min(kTopoMaxZoom, settings.topoZoom + dz));
                    break;
                }
            }
            if (e.x < 120) { screen = (ui::ScreenId)((screen + ui::SCR_COUNT - 1) % ui::SCR_COUNT); break; }
            if (e.x > 840 && screen != ui::SCR_MAP) { screen = (ui::ScreenId)((screen + 1) % ui::SCR_COUNT); break; }
            if (screen == ui::SCR_MAP) {
                if (e.x > 680) { screen = (ui::ScreenId)((screen + 1) % ui::SCR_COUNT); break; }
                settings.mapScaleIdx = (settings.mapScaleIdx + 1) % 6;
            } else if (screen == ui::SCR_NAV) {
                cycleTarget();
            }
            break;
        }
        default: return false;
    }
    return true;
}

// ------------------------------------------------------------------ rendering
static void render(bool full) {
    crumb(std::string("rysowanie: ") + (menu.isOpen() ? menu.describe() : ui::screenName(screen)) +
          (full ? " (pełne)" : ""));
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    ui::UiContext u;
    u.nav = &fc.state();
    u.set = &settings;
    u.asp = &airspace;
    u.hits = &hits;
    u.wpts = &waypoints;
    u.route = &route;
    u.fc = &fc;
    u.terrain = &terrain;
    u.alerts = &alerts;
    u.sys = &sys;
    u.topo = sys.sdOk ? &topo : nullptr;
    u.topoView = &topoView;
    if (usbMode) {
        ui::drawUsbDisk(c, u, usbdisk::hostConnected());
        if (powerArming) ui::drawPowerHint(c);
        board::displayUpdate(full);
        return;
    }
    if (menu.isOpen()) {
        menu.draw(c);
    } else if (showSummary) {
        ui::drawSummary(c, u);
    } else {
        ui::drawScreen(c, screen, u);
        int a = alerts.top();
        if (a >= 0) ui::drawAlertBanner(c, u, a);
    }
    if (!menu.isOpen()) ui::drawTransferBanner(c, u);
    if (powerArming) ui::drawPowerHint(c);
    board::displayUpdate(full);
}

static void bootScreen(const std::vector<std::string>& lines) {
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    ui::drawBoot(c, lines, PARANAV_VERSION);
    board::displayUpdate(false);
}

// ------------------------------------------------------------------ crash report
// After a panic the ESP-IDF writes a core dump to the "coredump" flash partition. Here we read its
// summary (task, program counter, backtrace) and save it to /sd/logs/crash-N.txt. With the firmware.elf
// of the same build, "xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <addresses>" shows the exact line.
static std::string gCrashCrumb;  // what the device was doing when it crashed (read at boot)
static std::string saveCrashReport() {
    if (esp_core_dump_image_check() != ESP_OK) return "";
    esp_core_dump_summary_t* sm = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
    if (!sm) return "";
    std::string shortTxt;
    if (esp_core_dump_get_summary(sm) == ESP_OK) {
        char head[160];
        snprintf(head, sizeof head, "zadanie %s, PC 0x%08x, przyczyna %u, adres 0x%08x", sm->exc_task,
                 (unsigned)sm->exc_pc, (unsigned)sm->ex_info.exc_cause, (unsigned)sm->ex_info.exc_vaddr);
        std::string bt;
        for (uint32_t i = 0; i < sm->exc_bt_info.depth && i < 16; i++) {
            char b[12];
            snprintf(b, sizeof b, "0x%08x ", (unsigned)sm->exc_bt_info.bt[i]);
            bt += b;
        }
        char sh[64];
        snprintf(sh, sizeof sh, "(%s, PC 0x%08x)", sm->exc_task, (unsigned)sm->exc_pc);
        shortTxt = sh;
        Serial.printf("[crash] %s\n[crash] backtrace: %s\n", head, bt.c_str());
        if (sys.sdOk) {
            mkdir(PATH_LOGS, 0777);
            for (int n = 1; n < 100; n++) {
                char path[48];
                snprintf(path, sizeof path, PATH_LOGS "/crash-%02d.txt", n);
                FILE* t = fopen(path, "r");
                if (t) {
                    fclose(t);
                    continue;
                }
                FILE* f = fopen(path, "w");
                if (f) {
                    fprintf(f, "ParaNav %s - crash report\n%s\nostatnia czynnosc: %s\nbacktrace: %s\n", PARANAV_VERSION,
                            head, gCrashCrumb.empty() ? "?" : gCrashCrumb.c_str(), bt.c_str());
                    fprintf(f, "\nDekodowanie: xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/t5s3pro/firmware.elf %s\n",
                            bt.c_str());
                    fclose(f);
                    shortTxt = std::string(path + strlen(SD_MOUNT) + 1) + " " + shortTxt;
                }
                break;
            }
        }
    }
    free(sm);
    esp_core_dump_image_erase();  // report once
    return shortTxt;
}

// ------------------------------------------------------------------ power off
static void shutdownDevice() {
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    if (board::usbPowered()) {
        // BQ25896 cannot cut power while USB is connected
        ui::drawPowerOff(c, "Odłącz kabel USB", "przy zasilaniu z USB wyłączenie nie jest możliwe");
        board::displayUpdate(true);
        delay(3000);
        return;
    }
    if (usbMode) {
        usbdisk::stop();
        usbMode = sys.usbDisk = false;
        sys.sdOk = true;
    }
    logger.stop();  // closes IGC/GPX properly
    sys.recording = false;
    saveSettings();
    SD.end();
    ui::drawPowerOff(c, "WYŁĄCZONY", "Włączanie: przycisk PWR lub podłączenie USB");
    board::displayUpdate(true);  // e-paper keeps this image without power
    board::powerOff();
    // if we get here the power-off failed
    sys.sdOk = board::sdInit();
    ui::drawPowerOff(c, "Nie udało się wyłączyć", "spróbuj ponownie lub odłącz baterię");
    board::displayUpdate(false);
    delay(3000);
}

// ------------------------------------------------------------------ setup / loop
void setup() {
    Serial.begin(115200);
    // 100 kHz like the LilyGO factory firmware (epdiy configures the same bus to 100 kHz anyway)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    // fuel gauge first, before epdiy takes over the I2C port - same order as the factory firmware
    bool batt = board::batteryInit();
    board::backlight(0);

    // SD first so config.ini (VCOM, flip) is known before the display starts
    sys.sdOk = board::sdInit();
    if (sys.sdOk) settings.load(PATH_CONFIG);
    usbdisk::init();  // USB drive registered without media (shown only in "Tryb USB")
#if !ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);  // never block on log output when no terminal reads the USB port
#endif
    board::displayInit(settings.vcomMv);
    // From now on every allocation of 32 bytes or more goes to the 8 MB PSRAM first (internal RAM as fallback).
    // Arduino's default threshold is 4 KB, so a real airspace file (thousands of small polygon buffers,
    // ~400 kB) filled the ~150 kB of internal RAM left after the e-paper driver -> out of memory -> panic.
    heap_caps_malloc_extmem_enable(32);
    // long file parsing must let the idle task run, otherwise the 5 s task watchdog resets the device
    nav::setYield([] { vTaskDelay(1); });

    std::vector<std::string> boot;
    {
        // tell the user if the previous run ended with a crash (helps reporting problems)
        esp_reset_reason_t rr = esp_reset_reason();
        const char* why = nullptr;
        switch (rr) {
            case ESP_RST_PANIC: why = "błąd programu (panic)"; break;
            case ESP_RST_INT_WDT:
            case ESP_RST_TASK_WDT:
            case ESP_RST_WDT: why = "zawieszenie (watchdog)"; break;
            case ESP_RST_BROWNOUT: why = "spadek napięcia (bateria?)"; break;
            default: break;
        }
        if (why) {
            boot.push_back(std::string("[!!] Poprzedni restart: ") + why);
            Serial.printf("[boot] reset reason %d\n", (int)rr);
            if (gCrumbMagic == kCrumbMagic) {
                gCrumb[sizeof gCrumb - 1] = 0;
                gCrashCrumb = gCrumb;
                boot.push_back(std::string("[!!] Ostatnio: ") + gCrumb);
                Serial.printf("[boot] last action: %s\n", gCrumb);
            }
        }
        if (why) sys.lastCrash = std::string(why) + (gCrumbMagic == kCrumbMagic ? std::string(": ") + gCrumb : "");
        crumb("start");
    }
    boot.push_back(std::string(sys.sdOk ? "[OK] " : "[!!] ") + "Karta SD" + (sys.sdOk ? "" : " – brak, dane i zapis lotu niedostępne"));
    bootScreen(boot);

    crumb("start: barometr");
    sys.baroOk = baro.begin(Wire);
    {
        char b[80];
        if (sys.baroOk) snprintf(b, sizeof b, "[OK] Barometr na kablu MS5611 (adres 0x%02X)", baro.address());
        else snprintf(b, sizeof b, "[--] Barometr na kablu: brak (użyję BlueFly albo GPS)");
        boot.push_back(b);
    }
    crumb("start: skan I2C");
    sys.i2cDevices = board::i2cScan();
    Serial.printf("[i2c] %s\n", sys.i2cDevices.c_str());
    boot.push_back("[..] I2C: " + sys.i2cDevices);
    crumb("start: bateria");
    board::Battery bt0 = board::batteryRead();
    {
        char b[96];
        snprintf(b, sizeof b, "%s Bateria: %s %.2f V", batt && bt0.ok ? "[OK]" : "[!!]", bt0.source, bt0.volts);
        boot.push_back(b);
    }
    if (sys.sdOk) {
        crumb("start: wczytywanie danych z SD");
        loadData();
        char b[96];
        snprintf(b, sizeof b, "[OK] Strefy %d, punkty %d, trasa %d, teren %d, mapy %d", sys.airspaceZones,
                 sys.waypoints, sys.routePoints, sys.terrainTiles, sys.topoAreas);
        boot.push_back(b);
        sys.sdFreeMb = board::sdFreeMb();
    }
    if (!sys.lastCrash.empty()) {
        std::string rep = saveCrashReport();
        sys.loadErrors.insert(sys.loadErrors.begin(), "Restart: " + sys.lastCrash);
        if (!rep.empty()) sys.loadErrors.insert(sys.loadErrors.begin() + 1, "Raport: " + rep);
    }
    boot.push_back("[..] GPS – wykrywanie modułu");
    bootScreen(boot);

    crumb("start: GPS");
    bool gpsOk = gps.begin();
    sys.gpsModule = gps.moduleName();
    boot.back() = std::string(gpsOk ? "[OK] GPS: " : "[!!] GPS: ") + gps.moduleName();
    bootScreen(boot);

    fc.setTerrain(&terrain);
    fc.setWaypoints(&waypoints);
    fc.setRoute(&route);
    board::inputStart(settings.flip180);
    phone::setEnabled(settings.phoneEnabled);
    bluefly::begin(settings.blueflyEnabled);
    xTaskCreatePinnedToCore(sensorTask, "sensors", 8192, nullptr, 5, nullptr, 1);
    delay(500);
    render(true);
    crumb("praca");
}

void loop() {
    static uint32_t lastSecond = 0, lastRender = 0, lastFull = 0, lastBatt = 0;
    static bool dirty = true;
    uint32_t now = millis();

    board::Event e;
    while (board::inputGet(e)) dirty |= handleEvent(e);

    bool newFix = false;
    GpsFix g;
    BaroSnapshot b;
    {
        std::lock_guard<std::mutex> l(shMtx);
        if (shNewFix) {
            newFix = true;
            shNewFix = false;
            g = shFix;
        }
        b = shBaro;
    }
    if (usbMode) {
        static uint32_t lastUsb = 0;
        static bool lastHost = false;
        if (usbdisk::takeEjected()) {  // "Eject" in the PC -> back to normal operation
            usbExit();
            dirty = true;
        } else if (now - lastUsb > 1000) {
            bool h = usbdisk::hostConnected();
            if (h != lastHost) dirty = true;
            lastHost = h;
            lastUsb = now;
        }
    } else if (newFix || now - lastSecond > 1500) {
        onSecond(newFix, g, b);
        lastSecond = now;
        dirty = true;
    }
    // files uploaded from the phone: install + reload here (main task owns the data)
    {
        nav::FileReceiver::Pending p;
        if (!usbMode && phone::takePending(p)) {
            crumb("instalacja pliku z telefonu: " + p.kind);
            if (p.kind == "reload") {
                loadData();
                phone::notify("OK RELOADED");
                phone::setLastResult("usunięto " + p.name);
            } else {
                std::string msg;
                int count = 0;
                bool ok = nav::FileReceiver::install(SD_MOUNT, p, msg, count);
                if (ok) loadData();
                static const char* what[] = {"airspace", "stref", "activations", "aktywacji", "waypoints", "punktów",
                                             "route", "punktów trasy"};
                const char* unit = "";
                for (int i = 0; i < 8; i += 2)
                    if (p.kind == what[i]) unit = what[i + 1];
                phone::setLastResult(ok ? std::to_string(count) + " " + unit + " – " + msg : "BŁĄD: " + msg);
                phone::notify(ok ? "OK END " + std::to_string(count) + " " + msg : "ERR " + msg);
            }
            dirty = true;
        }
    }
    {
        static uint32_t lastPh = 0;
        static float lastProg = -1;
        phone::Status ps = phone::status();
        int stt = !ps.enabled ? 0 : ps.receiving ? 3 : ps.connected ? 2 : 1;
        if (stt != sys.phoneState || (stt == 3 && ps.progress - lastProg >= 0.05f && now - lastPh > 1500)) {
            sys.phoneState = stt;
            sys.phoneProgress = ps.progress;
            lastProg = ps.progress;
            lastPh = now;
            dirty = true;
        }
        sys.phoneFile = ps.name;
        sys.phoneLast = ps.last;
    }
    {
        static uint32_t lastBf = 0;
        if (now - lastBf > 1000) {
            bluefly::Status bs = bluefly::status();
            int newState = (int)bs.state;
            if (newState != sys.bfState) dirty = true;
            sys.bfState = newState;
            sys.bfName = bs.name;
            sys.bfRssi = bs.rssi;
            sys.bfBattPct = bs.batteryPct;
            sys.bfBattV = bs.batteryV;
            sys.bfFormat = bs.format;
            sys.bfError = bs.error;
            lastBf = now;
        }
    }
    if (backlightOn && settings.backlightOffMin > 0 &&
        now - backlightSince > (uint32_t)settings.backlightOffMin * 60000UL) {
        setBacklight(false);
        dirty = true;
    }
    if (now - lastBatt > 10000 || lastBatt == 0) {
        board::Battery bt = board::batteryRead();
        sys.battPct = bt.pct;
        sys.battV = bt.volts;
        sys.battMa = bt.ma;
        sys.charging = bt.charging;
        sys.battSource = bt.source;
        sys.battMinLeft = bt.minutesLeft;
        sys.freeHeapKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
        sys.freePsramKb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        sys.gpsSentences = gps.sentences();
        sys.gpsCsErr = gps.checksumErrors();
        board::lastTouch(sys.touchX, sys.touchY);
        sys.baroOk = baro.ok();
        lastBatt = now;
    }
    if (dirty && now - lastRender >= 250) {
        bool full = settings.fullRefreshS > 0 && now - lastFull > (uint32_t)settings.fullRefreshS * 1000;
        render(full);
        if (full) lastFull = now;
        lastRender = now;
        dirty = false;
    }
    delay(10);
}
