// ParaNav - paramotor flight computer for LilyGO T5 E-Paper S3 Pro + MS5611
//
// Tasks:
//   sensorTask (core 1, prio 5): MS5611 @ ~90 Hz -> Kalman vario, GNSS NMEA parsing
//   inputTask  (core 1, prio 3): buttons + touch -> event queue           (board.cpp)
//   loop()     (Arduino task)  : 1 Hz flight logic, airspace, alerts, logger, UI rendering
#include <Arduino.h>
#include "i18n.h"
#include <SD.h>
#include <Wire.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <Preferences.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <strings.h>
#include <mutex>

#include "airspace.h"
#include "escape.h"
#include "alerts.h"
#include "board.h"
#include "config.h"
#include "diag.h"
#include "flight.h"
#include "flightlog.h"
#include "gps.h"
#include "logger.h"
#include "ms5611.h"
#include "bluefly_ble.h"
#include "phone_ble.h"
#include "settings.h"
#include "terrain.h"
#include "topo.h"
#include "fanet.h"
#include "fanet_radio.h"
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
// What the device is doing now, kept in RTC memory (see diag.h): after an unexpected restart the
// boot screen and /logs/crash-NN.txt show the last actions.
static void crumb(const std::string& s) { diag::crumb(s.c_str()); }
static std::vector<std::string> bootNotes;  // shown at the top of the SYSTEM screen errors

// ------------------------------------------------------------------ global state
static Settings settings;
static AirspaceDb airspace;
static WaypointDb waypoints;
static Route route;
static Terrain terrain;
static TopoMap topo;          // background map tiles (/sd/maps)
static TopoView topoView;     // TOPO screen view centre/zoom between frames
static bool usbMode = false;  // SD card handed over to the PC as a USB drive
static TrafficDb traffic;     // FANET aircraft around (decoded in loop(), radio runs in its own task)
static FlightComputer fc(settings);
static AlertManager alerts;
static TrackLogger logger;
static ui::SysInfo sys;
static std::vector<AirspaceHit> hits;
static ui::Menu menu;
static ui::ScreenId screen = ui::SCR_FLIGHT;
static bool showSummary = false;
static void shutdownDevice();

// ---- USB power: question "navigation / charging / files" and the charging mode
// Asked after a power-on with USB connected (default: charging) or when USB is plugged in on
// the ground (default: navigation). Never asked in flight, never after a crash restart.
enum class PowerUi : uint8_t { Normal, Ask, Charging };
static PowerUi powerUi = PowerUi::Normal;
static int askSel = 0, askDefault = ui::UC_NAV;
static uint32_t askSinceMs = 0;
static bool askAuto = true;  // automatic choice pending (cleared by any button / touch)
static constexpr uint32_t kAskTimeoutMs = 15000;
static bool usbPower = false;  // VBUS present (polled once per second)
static bool askFiles() { return usbdisk::supported() && usbdisk::hostConnected(); }
static bool powerArming = false;
static bool backlightOn = false;
static uint32_t backlightSince = 0;

static void setBacklight(bool on) {
    backlightOn = on;
    backlightSince = millis();
    board::backlight(on ? settings.backlightPct : 0);
    sys.backlight = on;
}
// route files in /route (name, points, length) for the "Choose route" menu
struct RouteInfo {
    std::string path, name;
    int points = 0;
    float km = 0;
};
static std::vector<RouteInfo> routeList;
static void selectRoute(const std::string& path);

// Last known GPS position, kept in flash (NVS) over power-off: the TOPO screen shows the map there
// while waiting for a fix, the power-off screen uses it for its map background.
static double lastLat = NAN, lastLon = NAN;
static double savedLat = NAN, savedLon = NAN;
static uint32_t lastPosSavedMs = 0;
static void loadLastPos() {
    Preferences p;
    if (!p.begin("paranav", true)) return;
    double la = p.getDouble("lat", NAN), lo = p.getDouble("lon", NAN);
    p.end();
    if (std::fabs(la) <= 90 && std::fabs(lo) <= 180) lastLat = savedLat = la, lastLon = savedLon = lo;
}
static void saveLastPos() {
    if (std::isnan(lastLat) || (lastLat == savedLat && lastLon == savedLon)) return;
    Preferences p;
    if (!p.begin("paranav", false)) return;
    p.putDouble("lat", lastLat);
    p.putDouble("lon", lastLon);
    p.end();
    savedLat = lastLat;
    savedLon = lastLon;
    lastPosSavedMs = millis();
}

// Saved flight viewer (Menu > Saved flights)
static bool flightViewOpen = false;
static nav::FlightLog viewedFlight;
static ui::FlightView flightView;

static MS5611 baro;
static GpsModule gps;

// wired barometer filter statistics for the SYSTEM screen: blanked full refreshes, rejected jump samples
static volatile uint32_t baroBlanked = 0, baroSpikes = 0;

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
    int spikes = 0;        // consecutive rejected wired samples
    bool inBlank = false;  // inside a full-refresh blanking window
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
        if (active == AltSource::Wired && wiredNew) {
            float h = baroAltitude(baro.pressureHpa(), 1013.25f);
            uint32_t nowUs = micros();
            if (board::baroBlanked()) {
                // full e-paper refresh: its high-voltage supply disturbs the wired sensor -> skip the
                // samples, the filter carries on with the last climb rate (lastUs is kept, so the next
                // good sample sees the whole gap)
                if (!inBlank) baroBlanked++;  // counts refreshes, not samples
                inBlank = true;
            } else {
                inBlank = false;
                if (!needReset && kv.initialized()) {
                    // a jump that no aircraft can do in one sample (>1.5 m vs. the prediction) is a glitch;
                    // if it persists for 30 samples (~0.3 s) the new level is real: move the filter there
                    float dt = (nowUs - lastUs) * 1e-6f;
                    float pred = kv.altitude() + kv.vario() * std::min(dt, 5.f);
                    bool jump = std::fabs(h - pred) > 1.5f;
                    if (jump) baroSpikes++;
                    if (!jump) {
                        spikes = 0;
                        feed(h, nowUs);
                    } else if (++spikes >= 30) {
                        spikes = 0;  // persistent: the new level is real, keep the climb rate
                        kv.rebase(h);
                        lastUs = nowUs;
                    }
                } else {
                    feed(h, nowUs);
                }
            }
        }
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

// PSRAM thresholds (heap_caps_malloc_extmem_enable): allocations of at least this size go to PSRAM first.
// While loading the card data everything from 32 B goes to PSRAM: a real airspace file is thousands of small
// polygon buffers that would fill the internal RAM. While running, short-lived small objects (strings,
// vectors built on every redraw / every second) use the internal RAM instead (PSRAM is the fallback when it is
// full). On ESP-IDF 4.4 (arduino-esp32 2.0.x) malloc/free walk the whole list of free blocks, so mixing this
// churn with the thousands of airspace buffers fragmented the PSRAM heap more and more and made every
// allocation slower - the UI got sluggish after 10-20 minutes.
static constexpr size_t kExtmemLoad = 32;
static constexpr size_t kExtmemRun = 1024;

static void loadDataImpl();
static void loadData() {
    heap_caps_malloc_extmem_enable(kExtmemLoad);
    struct RestoreExtmem {
        ~RestoreExtmem() { heap_caps_malloc_extmem_enable(kExtmemRun); }
    } restoreExtmem;  // also on the out-of-memory path below
    try {
        loadDataImpl();
    } catch (const std::bad_alloc&) {
        // never reboot because of too much data on the card - keep what fits and report it
        sys.loadErrors.push_back(TR("Za mało pamięci na wszystkie dane z karty SD", "Not enough memory for all data on the SD card"));
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
    sys.loadErrors = bootNotes;  // restart / safe mode notes stay on the SYSTEM screen
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
            if (k <= 0) sys.loadErrors.push_back(TR("Strefy: brak danych w ", "Airspace: no data in ") + n);
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

    routeList.clear();
    d = opendir(PATH_ROUTE);
    if (d) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n[0] == '.' || !endsWith(n, ".cup")) continue;
            Route r;
            RouteInfo ri;
            ri.path = std::string(PATH_ROUTE) + "/" + n;
            ri.name = n.substr(0, n.size() - 4);
            ri.points = r.loadCup(ri.path.c_str());
            ri.km = r.lengthM() / 1000.f;
            if (ri.points > 0) routeList.push_back(ri);
            maybeYield(1);
        }
        closedir(d);
    }
    std::sort(routeList.begin(), routeList.end(), [](const RouteInfo& a, const RouteInfo& b) { return a.name < b.name; });
    // the route chosen last time (config.ini: routeFile), else the first one
    std::string want;
    for (auto& r : routeList)
        if (r.path.substr(r.path.find_last_of('/') + 1) == settings.routeFile) want = r.path;
    if (want.empty() && !routeList.empty()) want = routeList.front().path;
    route = Route{};
    if (!want.empty()) route.loadCup(want.c_str());
    sys.routePoints = (int)route.pts.size();

    terrain.setDir(PATH_TERRAIN);
    sys.terrainTiles = terrain.countTiles();

    topo.setDir(diag::boot().safeMode ? "/nonexistent" : PATH_MAPS);  // safe mode: no maps
    sys.topoAreas = topo.scan();
    if (topo.errors()) sys.loadErrors.push_back(TR("Mapy: ", "Maps: ") + topo.lastError());
    topoView.valid = false;
}

static void saveSettings() {
    if (sys.sdOk) settings.save(PATH_CONFIG);
}

// Make a route file the active route (remembered in config.ini)
static void selectRoute(const std::string& path) {
    route = Route{};
    route.loadCup(path.c_str());
    sys.routePoints = (int)route.pts.size();
    settings.routeFile = path.substr(path.find_last_of('/') + 1);
    saveSettings();
}

// ---- airspace exit ("lead me out") and route check
static bool escapeMode = false;        // escape screen shown instead of the normal screens
static EscapeResult escapeRes;
static uint32_t escapeOutSince = 0;    // uptime [s] when we left the zone (0 = still inside)
static bool showRouteCheck = false;    // route check result shown (closed by any key / tap)
static std::vector<RouteConflict> routeCheck;

static void updateEscape() {
    const NavState& n = fc.state();
    if (!n.fix) return;  // keep the last plan
    EscapeQuery q;
    q.lat = n.lat;
    q.lon = n.lon;
    q.altMsl = n.altMsl;
    q.groundMsl = n.groundMsl;
    q.qnhHpa = settings.qnhHpa;
    q.trackDeg = n.trackDeg;
    q.gsMs = n.gsKmh / 3.6f;
    q.cruiseMs = settings.cruiseKmh / 3.6f;
    q.climbMs = settings.escClimbMs;
    q.sinkMs = settings.escSinkMs;
    q.utcSec = n.utcSec;
    q.dateYmd = n.dateYmd;
    q.maxAltMsl = nav::maxAltitudeMsl(settings.maxFl, settings.qnhHpa);
    escapeRes = planEscape(airspace, q, &settings);
}

static void startEscape() {
    crumb(TR("wyjście ze strefy: start", "airspace exit: start"));
    escapeMode = true;
    escapeOutSince = 0;
    alerts.ack(AL_AIRSPACE_IN, fc.state().uptimeS);
    updateEscape();
}

// Zones crossed by the active route; returns the number of conflicts (shown on screen when show)
static int checkActiveRoute(bool show) {
    std::vector<LatLon> path;
    for (const auto& w : route.pts) path.push_back({w.lat, w.lon});
    const NavState& n = fc.state();
    routeCheck = nav::checkRoute(airspace, path, &settings, n.utcSec, n.dateYmd, settings.qnhHpa, settings.routeCheckAglM,
                                 [](double la, double lo) { return terrain.elevation(la, lo); },
                                 nav::maxAltitudeMsl(settings.maxFl, settings.qnhHpa));
    int c = 0;
    for (auto& r : routeCheck) c += r.conflict;
    crumb(std::string(TR("kontrola trasy: kolizje ", "route check: conflicts ")) + std::to_string(c));
    if (show && path.size() >= 2) {
        menu.close();
        showRouteCheck = true;
    }
    return c;
}

// ------------------------------------------------------------------ language
static void applyLanguage() { setLang(settings.lang == "en" ? Lang::EN : Lang::PL); }
static bool menuRebuild = false;  // language changed inside the menu: rebuild it after the key is handled

// ------------------------------------------------------------------ USB drive mode
static void showMessage(const char* l1, const char* l2, uint32_t ms) {
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    ui::drawPowerOff(c, l1, l2);
    board::displayUpdate(false);
    delay(ms);
}

static void render(bool full);
static void applyFanetConfig();

// Hands the SD card over to the PC. All file access stops first (IGC log closed, map/terrain
// files closed, phone uploads off); sys.sdOk = false blocks settings saves while in this mode.
static void usbEnter() {
    if (!usbdisk::supported()) {
        showMessage(TR("Tryb USB niedostępny", "USB mode not available"), usbdisk::lastError(), 2500);
        return;
    }
    if (fc.state().phase == Phase::Flying) {
        showMessage(TR("Tryb USB niedostępny w locie", "USB mode not available in flight"), TR("wyląduj i zakończ lot", "land and end the flight"), 2500);
        return;
    }
    if (!sys.sdOk) {
        showMessage(TR("Brak karty SD", "No SD card"), TR("włóż kartę i uruchom ponownie", "insert a card and restart"), 2500);
        return;
    }
    if (sys.phoneState == 3) {
        showMessage(TR("Trwa odbiór z telefonu", "Receiving from the phone"), TR("poczekaj na koniec przesyłania", "wait until the transfer ends"), 2500);
        return;
    }
    crumb(TR("tryb USB: start", "USB mode: start"));
    logger.stop();
    sys.recording = false;
    saveSettings();
    phone::setEnabled(false);
    topo.close();
    terrain.setDir(PATH_TERRAIN);  // closes open .hgt files
    sys.sdOk = false;
    // radios off while the PC owns the card (the LoRa radio shares the SPI bus with the SD card)
    if (settings.blueflyEnabled) bluefly::setEnabled(false);
    fanet::Config off;
    off.rx = off.tx = false;
    fanet::configure(off);
    // draw the USB screen first: a full e-paper refresh while the computer reads the card came out
    // faint (the refresh lost its timing under the USB load)
    usbMode = true;
    sys.usbDisk = true;
    menu.close();
    render(true);
    if (!usbdisk::start()) {
        usbMode = false;
        sys.usbDisk = false;
        sys.sdOk = true;
        phone::setEnabled(settings.phoneEnabled);
        if (settings.blueflyEnabled) bluefly::setEnabled(true);
        applyFanetConfig();
        showMessage(TR("Tryb USB: błąd", "USB mode: error"), usbdisk::lastError(), 2500);
        return;
    }
}

// Takes the card back: remount (the PC changed the FAT) and reload everything.
static void usbExit() {
    crumb(TR("tryb USB: koniec, ponowne wczytanie", "USB mode: end, reloading"));
    usbdisk::stop();
    usbMode = false;
    sys.usbDisk = false;
    SD.end();
    sys.sdOk = board::sdInit();
    if (sys.sdOk) {
        settings.load(PATH_CONFIG);
        applyLanguage();
        loadData();
        sys.sdFreeMb = board::sdFreeMb();
    } else {
        sys.loadErrors.push_back(TR("Karta SD nie odpowiada po trybie USB", "SD card not responding after USB mode"));
    }
    phone::setEnabled(settings.phoneEnabled);
    if (settings.blueflyEnabled) bluefly::setEnabled(true);
    applyFanetConfig();
}

// ------------------------------------------------------------------ USB power modes
static void applyFanetConfig();

static void askUsbMode(int def) {
    crumb(TR("USB: pytanie o tryb", "USB: mode question"));
    powerUi = PowerUi::Ask;
    askDefault = def;
    askSel = def;
    askSinceMs = millis();
    askAuto = true;
}

static void enterCharging() {
    crumb(TR("tryb ładowania", "charging mode"));
    powerUi = PowerUi::Charging;
    menu.close();
    showSummary = false;
    if (settings.blueflyEnabled) bluefly::setEnabled(false);
    fanet::Config off;
    off.rx = off.tx = false;
    fanet::configure(off);
    setBacklight(false);
}

static void leaveCharging() {
    crumb(TR("ładowanie: koniec, nawigacja", "charging: end, navigation"));
    powerUi = PowerUi::Normal;
    if (settings.blueflyEnabled) bluefly::setEnabled(true);
    applyFanetConfig();
}

static void chooseUsbMode(int choice) {
    powerUi = PowerUi::Normal;
    if (choice == ui::UC_CHARGE) enterCharging();
    else if (choice == ui::UC_FILES) usbEnter();
}

// Once per second from loop(): react to USB being plugged / unplugged.
static void pollUsbPower(uint32_t now) {
    static uint32_t last = 0;
    if (now - last < 1000) return;
    last = now;
    bool p = board::usbPowered();
    bool plugged = p && !usbPower, unplugged = !p && usbPower;
    usbPower = p;
    bool flying = fc.state().phase == Phase::Flying;
    if (powerUi == PowerUi::Charging) {
        if (unplugged || flying) leaveCharging();  // flying: safety net, should not happen
    } else if (powerUi == PowerUi::Ask) {
        if (unplugged || flying) powerUi = PowerUi::Normal;
        else if (askAuto && now - askSinceMs >= kAskTimeoutMs) chooseUsbMode(askDefault == ui::UC_FILES ? ui::UC_NAV : askDefault);
        else if (askSel == ui::UC_FILES && !askFiles()) askSel = ui::UC_NAV;
    } else if (plugged && !flying && !usbMode && !menu.isOpen()) {
        askUsbMode(ui::UC_NAV);  // device was already navigating: stays in navigation if unanswered
    }
}

// ------------------------------------------------------------------ menu
static std::string onOff(bool b) { return b ? TR("TAK", "YES") : TR("NIE", "NO"); }

__attribute__((noinline)) static ui::MenuItem menuLot() {
    using M = ui::MenuItem;
    std::vector<M> lot;
    lot.push_back(M::act(TR("Ustaw START tutaj", "Set HOME here"), [] { fc.setHomeHere(); }, true));
    lot.push_back(M::toggle(TR("Silnik pracuje", "Engine running"), [] { return onOff(fc.state().engineOn); },
                  [] { fc.setEngine(!fc.state().engineOn); }));
    lot.push_back(M::act(TR("Zakończ lot (zamknij IGC)", "End flight (close IGC)"),
               [] {
                   logger.stop();
                   sys.recording = false;
                   fc.resetFlight();
               },
               true));
    return M::sub(TR("Lot", "Flight"), std::move(lot));
}

__attribute__((noinline)) static ui::MenuItem menuFuel() {
    using M = ui::MenuItem;
    std::vector<M> fuel;
    fuel.push_back(M::num(TR("Paliwo na starcie", "Fuel at takeoff"), [] { return settings.fuelStartL; }, [](float v) { settings.fuelStartL = v; }, 0, 40,
               0.5f, 1, "l"));
    fuel.push_back(M::num(TR("Zużycie", "Fuel flow"), [] { return settings.fuelFlowLph; }, [](float v) { settings.fuelFlowLph = v; }, 0.5f, 20, 0.1f,
               1, "l/h"));
    fuel.push_back(M::num(TR("Rezerwa", "Reserve"), [] { return settings.fuelReserveMin; }, [](float v) { settings.fuelReserveMin = v; }, 0, 60,
               5, 0, "min"));
    fuel.push_back(M::num(TR("Prędkość przelotowa", "Cruise speed"), [] { return settings.cruiseKmh; }, [](float v) { settings.cruiseKmh = v; }, 15,
               90, 1, 0, "km/h"));
    fuel.push_back(M::act(TR("Zatankowano do pełna", "Refuelled (full tank)"), [] { fc.refuel(settings.fuelStartL); }, true));
    return M::sub(TR("Paliwo", "Fuel"), std::move(fuel));
}

__attribute__((noinline)) static ui::MenuItem menuAlti() {
    using M = ui::MenuItem;
    std::vector<M> alti;
    alti.push_back(M::num("QNH", [] { return settings.qnhHpa; }, [](float v) { fc.setQnh(v); }, 950, 1060, 0.5f, 1, "hPa"));
    alti.push_back(M::num(TR("Wysokość teraz", "Altitude now"), [] { return std::isnan(fc.state().altMsl) ? 0.f : fc.state().altMsl; },
               [](float v) { fc.setAltitude(v); }, -100, 5000, 1, 0, "m"));
    alti.push_back(M::toggle(TR("Auto QNH (GPS/teren)", "Auto QNH (GPS/terrain)"), [] { return onOff(settings.autoQnh); },
                  [] { settings.autoQnh = !settings.autoQnh; }));
    alti.push_back(M::act(TR("Kalibruj ponownie z GPS", "Recalibrate from GPS"),
               [] {
                   settings.autoQnh = true;
                   fc.mutableState().qnhCalibrated = false;
               },
               true));
    return M::sub(TR("Wysokościomierz", "Altimeter"), std::move(alti));
}

__attribute__((noinline)) static ui::MenuItem menuNav() {
    using M = ui::MenuItem;
    std::vector<M> navm;
    navm.push_back(M::act(TR("Cel: START (Go Home)", "Target: HOME (Go Home)"), [] { fc.setTargetHome(); screen = ui::SCR_NAV; }, true));
    navm.push_back(M::act(TR("Cel: najbliższe lądowisko", "Target: nearest landing"), [] { fc.setTargetNearestLanding(); screen = ui::SCR_NAV; }, true));
    navm.push_back(M::act(TR("Wyprowadź ze strefy", "Lead me out of airspace"), [] {
        updateEscape();
        if (escapeRes.inside) startEscape();
        else showMessage(TR("Nie jesteś w strefie", "You are not inside airspace"), TR("(strefy aktywne, nie ignorowane)", "(active, not ignored zones)"), 2000);
    }, true));
    if (route.pts.size() >= 2)
        navm.push_back(M::act(TR("Sprawdź trasę – strefy", "Check route – airspace"), [] { checkActiveRoute(true); }, true));
    if (!route.pts.empty()) {
        navm.push_back(M::act(TR("Trasa ", "Route ") + route.name.substr(0, route.name.size() - 4) + ": start", [] { fc.setTargetRoute(0); screen = ui::SCR_NAV; }, true));
        navm.push_back(M::act(TR("Trasa: następny punkt", "Route: next point"), [] { fc.routeNext(); }, true));
        navm.push_back(M::act(TR("Trasa: poprzedni punkt", "Route: previous point"), [] { fc.routePrev(); }, true));
    }
    if (!routeList.empty()) {
        // all routes on the card: "● name  (6 pts, 48 km)", selecting one flies its first point
        std::vector<M> list;
        for (const auto& r : routeList) {
            char lbl[96];
            snprintf(lbl, sizeof lbl, TR("%s%s  (%d pkt, %.0f km)", "%s%s  (%d pts, %.0f km)"),
                     r.path == route.path ? "● " : "", r.name.c_str(), r.points, (double)r.km);
            std::string p = r.path;
            list.push_back(M::act(lbl, [p] {
                selectRoute(p);
                fc.setTargetRoute(0);
                screen = ui::SCR_NAV;
                checkActiveRoute(true);
            }, true));
        }
        navm.push_back(M::sub(TR("Wybierz trasę", "Choose route"), list));
    }
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
        if (!list.empty()) navm.push_back(M::sub(TR("Cel: punkt z listy", "Target: waypoint from list"), list));
    }
    return M::sub(TR("Nawigacja", "Navigation"), std::move(navm));
}

__attribute__((noinline)) static ui::MenuItem menuGlide() {
    using M = ui::MenuItem;
    std::vector<M> glide;
    glide.push_back(M::num(TR("Doskonałość (bez silnika)", "Glide ratio (engine off)"), [] { return settings.glideRatio; }, [](float v) { settings.glideRatio = v; },
               2, 12, 0.1f, 1, ""));
    glide.push_back(M::num(TR("Prędkość ślizgu", "Glide speed"), [] { return settings.glideSpeedKmh; }, [](float v) { settings.glideSpeedKmh = v; }, 20,
               60, 1, 0, "km/h"));
    glide.push_back(M::num(TR("Zapas wysokości", "Safety height"), [] { return settings.glideMarginM; }, [](float v) { settings.glideMarginM = v; }, 0,
               500, 10, 0, "m"));
    return M::sub(TR("Szybowanie", "Glide"), std::move(glide));
}

__attribute__((noinline)) static ui::MenuItem menuAlarms() {
    using M = ui::MenuItem;
    std::vector<M> alarms;
    alarms.push_back(M::num(TR("Strefa: ostrzeż przed", "Airspace: warn before"), [] { return settings.asHorizWarnS; }, [](float v) { settings.asHorizWarnS = v; },
               10, 300, 10, 0, "s"));
    alarms.push_back(M::num(TR("Strefa: odległość", "Airspace: distance"), [] { return settings.asHorizWarnM; }, [](float v) { settings.asHorizWarnM = v; }, 100,
               3000, 100, 0, "m"));
    alarms.push_back(M::num(TR("Strefa: bufor pionowy", "Airspace: vertical buffer"), [] { return settings.asVertWarnM; }, [](float v) { settings.asVertWarnM = v; }, 0,
               500, 10, 0, "m"));
    alarms.push_back(M::num(TR("Niska wysokość (0=wył.)", "Low altitude (0=off)"), [] { return settings.lowAglWarnM; }, [](float v) { settings.lowAglWarnM = v; },
               0, 500, 10, 0, "m"));
    alarms.push_back(M::num(TR("Bateria", "Battery"), [] { return (float)settings.battWarnPct; }, [](float v) { settings.battWarnPct = (int)v; }, 0,
               50, 5, 0, "%"));
    alarms.push_back(M::num(TR("Wyjście ze strefy: wznoszenie", "Airspace exit: climb rate"), [] { return settings.escClimbMs; },
                            [](float v) { settings.escClimbMs = v; }, 0.5f, 5, 0.5f, 1, "m/s"));
    alarms.push_back(M::num(TR("Wyjście ze strefy: opadanie", "Airspace exit: sink rate"), [] { return settings.escSinkMs; },
                            [](float v) { settings.escSinkMs = v; }, 0.5f, 5, 0.5f, 1, "m/s"));
    alarms.push_back(M::num(TR("Kontrola trasy: lot do", "Route check: flying up to"), [] { return settings.routeCheckAglM; },
                            [](float v) { settings.routeCheckAglM = v; }, 100, 3000, 100, 0, TR("m n.t.", "m AGL")));
    alarms.push_back(M::num(TR("Maks. wysokość FL (0=bez)", "Max altitude FL (0=none)"), [] { return (float)settings.maxFl; },
                            [](float v) { settings.maxFl = (int)v; }, 0, 195, 5, 0, ""));
    return M::sub(TR("Alarmy", "Alarms"), std::move(alarms));
}

__attribute__((noinline)) static ui::MenuItem menuDisp() {
    using M = ui::MenuItem;
    std::vector<M> disp;
    disp.push_back(M::toggle("Język / Language", [] { return std::string(langEn() ? "English" : "Polski"); },
                             [] {
                                 settings.lang = langEn() ? "pl" : "en";
                                 applyLanguage();
                                 menuRebuild = true;
                             }));
    disp.push_back(M::toggle(TR("Mapa: północ u góry", "Map: north up"), [] { return onOff(settings.mapNorthUp); },
                  [] { settings.mapNorthUp = !settings.mapNorthUp; }));
    disp.push_back(M::num(TR("Mapa: skala (0-5)", "Map: scale (0-5)"), [] { return (float)settings.mapScaleIdx; },
               [](float v) { settings.mapScaleIdx = (int)v; }, 0, 5, 1, 0, ""));
    disp.push_back(M::num(TR("TOPO: skala (10-14)", "TOPO: zoom (10-14)"), [] { return (float)settings.topoZoom; },
               [](float v) { settings.topoZoom = (int)v; }, 10, 14, 1, 0, ""));
    disp.push_back(M::toggle(TR("Obróć ekran o 180°", "Rotate screen 180°"), [] { return onOff(settings.flip180); },
                  [] {
                      settings.flip180 = !settings.flip180;
                      board::inputSetFlip(settings.flip180);
                  }));
    disp.push_back(M::num(TR("Pełne odświeżanie co", "Full refresh every"), [] { return (float)settings.fullRefreshS; },
               [](float v) { settings.fullRefreshS = (int)v; }, 0, 600, 30, 0, "s"));
    disp.push_back(M::num(TR("Strefa czasowa (UTC+)", "Time zone (UTC+)"), [] { return settings.tzOffsetMin / 60.f; },
               [](float v) { settings.tzOffsetMin = (int)(v * 60); }, -12, 14, 1, 0, "h"));
    disp.push_back(M::toggle(TR("Podświetlenie", "Front light"), [] { return onOff(backlightOn); }, [] { setBacklight(!backlightOn); }));
    disp.push_back(M::num(TR("Jasność podświetlenia", "Front light brightness"), [] { return (float)settings.backlightPct; },
                          [](float v) {
                              settings.backlightPct = (int)v;
                              if (backlightOn) board::backlight(settings.backlightPct);
                          },
                          5, 100, 5, 0, "%"));
    disp.push_back(M::num(TR("Podświetl. wyłącz po (0=nigdy)", "Light off after (0=never)"), [] { return (float)settings.backlightOffMin; },
                          [](float v) { settings.backlightOffMin = (int)v; }, 0, 60, 1, 0, "min"));
    return M::sub(TR("Ekran i mapa", "Display and map"), std::move(disp));
}

static void applyFanetConfig() {
    fanet::Config c;
    c.rx = settings.fanetEnabled;
    c.tx = settings.fanetEnabled && settings.fanetTx;
    c.addr = ((uint32_t)kFanetManufacturer << 16) | (uint16_t)settings.fanetId;
    c.name = settings.pilot == "Pilot" ? "" : settings.pilot;  // default placeholder: no name packet
    fanet::configure(c);
}

__attribute__((noinline)) static ui::MenuItem menuFanet() {
    using M = ui::MenuItem;
    std::vector<M> f;
    f.push_back(M::toggle(TR("FANET (radio LoRa)", "FANET (LoRa radio)"), [] { return onOff(settings.fanetEnabled); },
                          [] {
                              settings.fanetEnabled = !settings.fanetEnabled;
                              applyFanetConfig();
                          }));
    f.push_back(M::toggle(TR("Nadawaj moją pozycję", "Transmit my position"), [] { return onOff(settings.fanetTx); },
                          [] {
                              settings.fanetTx = !settings.fanetTx;
                              applyFanetConfig();
                          }));
    f.push_back(M::toggle(TR("Widoczny online (OGN)", "Visible online (OGN)"), [] { return onOff(settings.fanetOnline); },
                          [] { settings.fanetOnline = !settings.fanetOnline; }));
    f.push_back(M::toggle(TR("Ostrzeżenia o ruchu", "Traffic warnings"), [] { return onOff(settings.trafficAlarm); },
                          [] { settings.trafficAlarm = !settings.trafficAlarm; }));
    f.push_back(M::num(TR("Ostrzeż: odległość", "Warn: distance"), [] { return settings.trafficWarnM; },
                       [](float v) { settings.trafficWarnM = v; }, 100, 2000, 50, 0, "m"));
    f.push_back(M::num(TR("Ostrzeż: różnica wysokości", "Warn: height difference"), [] { return settings.trafficVertM; },
                       [](float v) { settings.trafficVertM = v; }, 30, 500, 10, 0, "m"));
    return M::sub(TR("FANET – inni piloci", "FANET – other pilots"), std::move(f));
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
    sysm.push_back(M::act(TR("BlueFly: połącz ponownie", "BlueFly: reconnect"), [] { bluefly::reconnect(); }, true));
    sysm.push_back(M::toggle(TR("Telefon (Bluetooth)", "Phone (Bluetooth)"), [] { return onOff(settings.phoneEnabled); },
                             [] {
                                 settings.phoneEnabled = !settings.phoneEnabled;
                                 phone::setEnabled(settings.phoneEnabled);
                                 if (settings.phoneEnabled) bluefly::retryStack();
                             }));
    sysm.push_back(M::act(TR("Wczytaj ponownie dane z SD", "Reload data from SD"), [] { loadData(); }, true));
    sysm.push_back(M::act(TR("Tryb USB – karta SD jako dysk", "USB mode – SD card as a drive"), [] { usbEnter(); }, true));
    sysm.push_back(M::act(TR("Tryb ładowania (przy USB)", "Charging mode (on USB)"), [] {
        if (!board::usbPowered()) showMessage(TR("Brak zasilania USB", "No USB power"), TR("podłącz ładowarkę", "connect a charger"), 2000);
        else if (fc.state().phase == Phase::Flying) showMessage(TR("Niedostępne w locie", "Not available in flight"), "", 2000);
        else enterCharging();
    }, true));
    sysm.push_back(M::num(TR("VCOM e-papieru (restart)", "E-paper VCOM (restart)"), [] { return (float)settings.vcomMv; },
               [](float v) { settings.vcomMv = (int)v; }, 1000, 3000, 10, 0, "mV"));
    return M::sub("System", std::move(sysm));
}

__attribute__((noinline)) static ui::MenuItem menuOff() {
    using M = ui::MenuItem;
    std::vector<M> off;
    off.push_back(M::act(TR("TAK – zapisz lot i wyłącz", "YES – save flight and power off"), [] { shutdownDevice(); }, true));
    return M::sub(TR("Wyłącz urządzenie", "Power off"), std::move(off));
}

__attribute__((noinline)) static ui::MenuItem menuHelp() {
    std::vector<ui::MenuItem> help;
    help.reserve(ui::helpTopics().size());
    for (const auto& t : ui::helpTopics()) help.push_back(ui::MenuItem::page(t.title, t.text));
    return ui::MenuItem::sub(TR("Pomoc", "Help"), std::move(help));
}

// The menu is built on demand. Every sub-menu has its own (non-inlined) function so that the
// temporaries do not pile up in one huge stack frame - one big function needed >7 KB of stack
// and overflowed the 8 KB Arduino loop task (device reset on opening the menu).
static void openFlight(const std::string& path, const std::string& name) {
    if (fc.state().phase == Phase::Flying) {
        showMessage(TR("Niedostępne w locie", "Not available in flight"), TR("loty przejrzysz po lądowaniu", "view the flights after landing"), 2000);
        return;
    }
    crumb(TR("lot: otwieram", "flight: opening"));
    std::string err;
    viewedFlight = nav::FlightLog();
    if (!nav::loadFlight(path, viewedFlight, 6000, &err)) {
        showMessage(TR("Nie mogę odczytać lotu", "Cannot read the flight"), name.c_str(), 2000);
        return;
    }
    flightView = ui::FlightView();
    flightView.log = &viewedFlight;
    flightView.title = name;
    ui::flightViewFit(flightView);
    flightViewOpen = true;
}

static ui::MenuItem menuFlights() {
    using M = ui::MenuItem;
    std::vector<M> list;
    auto files = sys.sdOk ? nav::listFlights(PATH_LOGS, 60) : std::vector<nav::FlightFileInfo>();
    for (const auto& f : files) {
        char l[48], v[16];
        int loc = f.startSec < 0 ? -1 : ((f.startSec + settings.tzOffsetMin * 60) % 86400 + 86400) % 86400;
        if (loc >= 0) snprintf(l, sizeof l, "%02d.%02d.%04d  %02d:%02d", f.day, f.month, f.year, loc / 3600, (loc / 60) % 60);
        else snprintf(l, sizeof l, "%s", f.name.c_str());
        snprintf(v, sizeof v, "%d:%02d h", f.durationS / 3600, (f.durationS / 60) % 60);
        std::string path = f.path, name = f.name, val = v;
        M it = M::act(l, [path, name] { openFlight(path, name); }, true);
        it.value = [val] { return val; };
        list.push_back(std::move(it));
    }
    if (list.empty())
        list.push_back(M::page(TR("Brak zapisanych lotów", "No saved flights"),
                               TR("Loty zapisują się same w katalogu logs/ na karcie SD (pliki IGC i GPX), od startu do lądowania.",
                                  "Flights are recorded automatically to logs/ on the SD card (IGC and GPX files), from takeoff to landing.")));
    return M::sub(TR("Zapisane loty", "Saved flights"), std::move(list));
}

static std::shared_ptr<ui::MenuItem> buildMenu() {
    using M = ui::MenuItem;
    auto root = std::make_shared<M>();
    root->kind = M::Submenu;
    root->label = "root";
    root->children.reserve(12);
    root->children.push_back(menuLot());
    root->children.push_back(menuFlights());
    root->children.push_back(menuFuel());
    root->children.push_back(menuAlti());
    root->children.push_back(menuNav());
    root->children.push_back(menuGlide());
    root->children.push_back(menuAlarms());
    root->children.push_back(menuFanet());
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

    // FANET: alarm level (half the caution distance / height, or predicted to get there within 20 s)
    {
        const Traffic* t = traffic.list().empty() ? nullptr : &traffic.list()[0];
        if (flying && settings.trafficAlarm && t && t->level >= 3) {
            float rel = (float)wrap180(t->brgDeg - n.trackDeg);
            snprintf(b, sizeof b, TR("RUCH! godz. %d, %d m", "TRAFFIC! %d o'clock, %d m"), clockPosition(rel), (int)std::lround(t->distM));
            std::string title = b;
            snprintf(b, sizeof b, "%s  %+d m%s", t->name.empty() ? fanetAddrStr(t->addr).c_str() : t->name.c_str(),
                     (int)std::lround(t->dzM), t->distM > t->cpaM + 30 ? TR("  – zbliża się", "  – closing") : "");
            alerts.set(AL_TRAFFIC, true, now, title, b, (int)(t->addr & 0x7FFFFFFF));
        } else {
            alerts.set(AL_TRAFFIC, false, now);
        }
    }

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
        alerts.set(AL_AIRSPACE_IN, flying, now, TR("W STREFIE: ", "INSIDE: ") + a.name, b, in->idx);
    } else {
        alerts.set(AL_AIRSPACE_IN, false, now);
    }
    if (near) {
        const Airspace& a = airspace.zones()[near->idx];
        if (near->insideH)
            snprintf(b, sizeof b, "%s %d m", near->vpos == VertPos::Below ? TR("do podstawy", "to floor") : TR("do pułapu", "to ceiling"),
                     (int)near->vertMarginM);
        else if (near->timeToEntryS >= 0)
            snprintf(b, sizeof b, TR("wlot za %s  (%.1f km)", "entry in %s  (%.1f km)"), ui::fmtDur(near->timeToEntryS, false).c_str(),
                     near->distM / 1000.f);
        else
            snprintf(b, sizeof b, TR("odległość %.1f km", "distance %.1f km"), near->distM / 1000.f);
        alerts.set(AL_AIRSPACE_NEAR, flying, now, TR("STREFA: ", "AIRSPACE: ") + a.name, b, near->idx);
    } else {
        alerts.set(AL_AIRSPACE_NEAR, false, now);
    }
    bool fuelRet = flying && n.homeSet && n.fuelConfigured && n.returnMarginMin < 0;
    snprintf(b, sizeof b, TR("paliwo %.1f l, na powrót potrzeba %.1f l", "fuel %.1f l, return needs %.1f l"), n.fuelLeftL, n.returnNeedL);
    alerts.set(AL_FUEL_RETURN, fuelRet, now, TR("WRACAJ – PALIWO!", "TURN BACK – FUEL!"), b);
    bool fuelLow = flying && n.homeSet && n.fuelConfigured && n.returnMarginMin >= 0 && n.returnMarginMin < 10;
    snprintf(b, sizeof b, TR("zapas na powrót tylko %d min", "return margin only %d min"), (int)n.returnMarginMin);
    alerts.set(AL_FUEL_LOW, fuelLow, now, TR("MAŁO PALIWA", "LOW FUEL"), b);
    bool low = flying && settings.lowAglWarnM > 0 && !std::isnan(n.aglTerrain) && n.aglTerrain < settings.lowAglWarnM;
    snprintf(b, sizeof b, TR("%d m nad terenem", "%d m AGL"), (int)n.aglTerrain);
    alerts.set(AL_LOW_AGL, low, now, TR("NISKO!", "LOW!"), b);
    alerts.set(AL_GPS_LOST, flying && n.fixAgeS > 5, now, TR("BRAK GPS", "NO GPS"), TR("nawigacja i strefy nieaktualne", "navigation and airspace not updated"));
    // barometer lost during the flight (cable unplugged / BlueFly out of range)
    static bool hadBaro = false;
    if (n.baroOk) hadBaro = true;
    if (!flying) hadBaro = n.baroOk;
    alerts.set(AL_BARO_LOST, flying && hadBaro && !n.baroOk, now, TR("BRAK BAROMETRU", "NO BAROMETER"),
               TR("wysokość i vario z GPS (mniej dokładne)", "altitude and vario from GPS (less accurate)"));
    snprintf(b, sizeof b, "%d %%", sys.battPct);
    alerts.set(AL_BATTERY, sys.battPct >= 0 && sys.battPct < settings.battWarnPct && !sys.charging, now,
               TR("SŁABA BATERIA", "LOW BATTERY"), b);
}

// ------------------------------------------------------------------ 1 Hz cycle
static void onSecond(bool haveFix, const GpsFix& g, const BaroSnapshot& b) {
    diag::crumb(menu.isOpen() ? TR("obliczenia 1 Hz (w menu)", "1 Hz computation (in menu)") : TR("obliczenia 1 Hz", "1 Hz computation"));
    uint32_t up = millis() / 1000;
    if (haveFix) fc.update(g, b, up);
    else fc.updateNoGps(b, up);
    const NavState& n = fc.state();
    if (n.fix) {
        lastLat = n.lat;
        lastLon = n.lon;
        bool far = std::isnan(savedLat) || distanceM({savedLat, savedLon}, {lastLat, lastLon}) > 1000;
        if (std::isnan(savedLat) || (far && millis() - lastPosSavedMs > 600000)) saveLastPos();
    }
    // FANET: own position for the radio, evaluate traffic, follow a pilot chosen as target
    {
        fanet::OwnTx o;
        o.valid = n.fix && n.fixAgeS < 3;
        o.flying = n.phase == Phase::Flying;
        o.pos.lat = n.lat;
        o.pos.lon = n.lon;
        o.pos.altM = n.gpsAlt;  // FANET devices send GPS altitude; compare like with like
        o.pos.speedKmh = n.gsKmh;
        o.pos.climbMs = n.vario1s;
        o.pos.headingDeg = n.trackDeg;
        o.pos.aircraft = FA_PARAGLIDER;
        o.pos.online = settings.fanetOnline;
        o.neighbours = traffic.recent(millis());
        fanet::setOwn(o);
        TrafficOwn me;
        me.valid = n.fix;
        me.flying = o.flying;
        me.lat = n.lat;
        me.lon = n.lon;
        me.altM = n.gpsAlt;
        me.speedKmh = n.gsKmh;
        me.trackDeg = n.trackDeg;
        me.climbMs = n.vario1s;
        me.warnM = settings.trafficWarnM;
        me.vertM = settings.trafficVertM;
        traffic.evaluate(me, millis());
        if (n.tgtKind == TargetKind::Point && fc.targetPointTag) {
            const Traffic* t = traffic.find(fc.targetPointTag);
            if (t && t->lastMs) fc.updateTargetPoint(t->lat, t->lon);
        }
        fanet::Status fs = fanet::status();
        sys.fanetState = fs.state;
        sys.fanetErr = fs.err;
        sys.fanetTxCount = fs.tx;
        sys.fanetRxCount = fs.rx;
        sys.fanetRssi = fs.rssi;
        sys.fanetOnline = settings.fanetOnline;
        sys.fanetId = fanetAddrStr(((uint32_t)kFanetManufacturer << 16) | (uint16_t)settings.fanetId);
    }
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
            sys.loadErrors.push_back(TR("Nie mogę utworzyć pliku IGC", "Cannot create the IGC file"));
        }
    }
    if (logger.active() && haveFix && n.timeValid) {
        LogPoint p{n.year, n.month, n.day, n.hour, n.minute, n.second, n.lat, n.lon, n.fix, n.altStd, n.gpsAlt, n.altMsl};
        logger.add(p);
    }
    if (fc.landingEvent) {
        saveLastPos();
        logger.stop();
        sys.recording = false;
        showSummary = true;
        saveSettings();
    }
    evaluateAlerts();
    // airspace exit screen: follow the plan, keep the "inside" alert quiet, close 10 s after leaving
    if (escapeMode) {
        if (fc.state().phase != Phase::Flying) {
            escapeMode = false;
        } else {
            updateEscape();
            const Alert& ai = alerts.get(AL_AIRSPACE_IN);
            if (ai.active && !ai.acked) alerts.ack(AL_AIRSPACE_IN, up);
            if (escapeRes.inside) escapeOutSince = 0;
            else if (!escapeOutSince) escapeOutSince = up;
            else if (up - escapeOutSince >= 10) {
                escapeMode = false;
                crumb(TR("wyjście ze strefy: poza strefą", "airspace exit: outside"));
            }
        }
    }
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
        case Ev::KeyShort: return TR("KLAWISZ krótko", "KEY short");
        case Ev::KeyLong: return TR("KLAWISZ długo", "KEY long");
        case Ev::BootShort: return TR("BOOT krótko", "BOOT short");
        case Ev::BootLong: return TR("BOOT długo", "BOOT long");
        case Ev::Tap: return TR("dotyk", "touch");
        case Ev::BothShort: return TR("oba krótko", "both short");
        case Ev::PowerArm:
        case Ev::PowerCancel:
        case Ev::PowerOff: return TR("oba długo", "both long");
        default: return "?";
    }
}

static bool handleEventImpl(const board::Event& e);
static bool handleEvent(const board::Event& e) {
    char where[64], b[96];
    if (menu.isOpen()) snprintf(where, sizeof where, "%s", menu.describe().c_str());
    else snprintf(where, sizeof where, "%s%s", TR("ekran ", "screen "), ui::screenName(screen));
    snprintf(b, sizeof b, "%s: %s", where, evName(e.type));
    diag::crumb(b);
    bool r = handleEventImpl(e);
    snprintf(b, sizeof b, "%s%s: %s", TR("po: ", "after: "), where, evName(e.type));
    diag::crumb(b);
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
    if (powerUi == PowerUi::Ask) {
        int n = askFiles() ? 3 : 2;
        switch (e.type) {
            case Ev::KeyShort: askSel = (askSel + 1) % n; break;
            case Ev::BootShort: askSel = (askSel + n - 1) % n; break;
            case Ev::KeyLong: chooseUsbMode(askSel); break;
            case Ev::BootLong: chooseUsbMode(ui::UC_NAV); break;
            case Ev::Tap: {
                int b = ui::usbAskButtonAt(e.x, e.y, n == 3);
                if (b >= 0) chooseUsbMode(b);
                break;
            }
            default: break;
        }
        askAuto = false;  // somebody is there: no automatic choice any more
        return true;
    }
    if (powerUi == PowerUi::Charging) {
        if (e.type == Ev::KeyLong || e.type == Ev::BootLong || e.type == Ev::Tap) leaveCharging();
        return true;
    }
    if (usbMode) {  // only "leave USB mode" here
        if (e.type == Ev::KeyLong || e.type == Ev::BootLong) usbExit();
        return true;
    }
    if (fc.state().landingAsk) {
        // "LANDED?" over everything: KEY / left button = still flying (default), BOOT / right button = landed
        int b = e.type == Ev::Tap ? ui::landingAskButtonAt(e.x, e.y) : -1;
        if (e.type == Ev::KeyShort || b == 0) {
            fc.answerStillFlying();
            crumb(TR("lądowanie?: nadal lecę", "landed?: still flying"));
            return true;
        }
        if (e.type == Ev::BootShort || b == 1) {
            fc.answerLanded();
            crumb(TR("lądowanie?: wylądowałem", "landed?: landed"));  // summary shows after the next GPS second
            return true;
        }
        if (e.type == Ev::Tap && e.y < 330) return true;  // the card itself: no accidental screen change
    }
    if (showRouteCheck) {
        if (e.type != Ev::None) showRouteCheck = false;
        return true;
    }
    if (showSummary) {
        showSummary = false;
        return true;
    }
    if (flightViewOpen && !menu.isOpen()) {
        ui::FlightViewAction a = ui::FV_NONE;
        if (flightView.askDelete) {  // KEY / left = cancel, BOOT / right = delete
            if (e.type == Ev::KeyShort || e.type == Ev::KeyLong) flightView.askDelete = false;
            else if (e.type == Ev::BootShort) a = ui::FV_DELETE, flightView.askDelete = false;
            else if (e.type == Ev::Tap) a = ui::flightViewTap(flightView, e.x, e.y);
        } else {
            switch (e.type) {
                case Ev::KeyShort: ui::flightViewZoom(flightView, +1); break;
                case Ev::BootShort: ui::flightViewZoom(flightView, -1); break;
                case Ev::KeyLong: a = ui::FV_CLOSE; break;
                case Ev::BootLong: flightView.askDelete = true; break;
                case Ev::Tap: a = ui::flightViewTap(flightView, e.x, e.y); break;
                default: break;
            }
        }
        if (a == ui::FV_DELETE) {
            std::string name = flightView.title;
            bool ok = nav::deleteFlight(viewedFlight.path);
            crumb(ok ? TR("lot usunięty", "flight deleted") : TR("lot: błąd usuwania", "flight: delete failed"));
            showMessage(ok ? TR("Lot usunięty", "Flight deleted") : TR("Nie udało się usunąć", "Could not delete"), name.c_str(), 1500);
            a = ui::FV_CLOSE;
        }
        if (a == ui::FV_CLOSE) {
            flightViewOpen = false;
            viewedFlight = nav::FlightLog();  // free the points
            flightView = ui::FlightView();
        }
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
        if (menuRebuild) {  // re-open in the new language (the old menu tree is no longer executing)
            menuRebuild = false;
            saveSettings();
            menu.open(buildMenu());
        }
        return true;
    }
    int top = alerts.top();
    if (top == AL_AIRSPACE_IN) {
        // two choices: KEY / left button = lead me out, BOOT / right button = ignore (reminds in 2 min)
        ui::AlertButton b = e.type == Ev::Tap ? ui::alertButtonAt(top, e.x, e.y) : ui::AB_NONE;
        if (e.type == Ev::KeyShort || b == ui::AB_ESCAPE) {
            startEscape();
            return true;
        }
        if (e.type == Ev::BootShort || b == ui::AB_IGNORE) {
            alerts.ack(top, fc.state().uptimeS);
            return true;
        }
        if (e.type == Ev::Tap && e.y < 280) return true;  // the banner itself: no accidental screen change
    } else if (top >= 0 && (e.type == Ev::KeyShort || e.type == Ev::BootShort || (e.type == Ev::Tap && e.y < 160))) {
        alerts.ack(top, fc.state().uptimeS);
        return true;
    }
    if (escapeMode) {  // escape screen: hold a key to close, everything else stays here
        if (e.type == Ev::KeyLong || e.type == Ev::BootLong) {
            escapeMode = false;
            crumb(TR("wyjście ze strefy: zamknięte", "airspace exit: closed"));
        }
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
            if (screen == ui::SCR_TRAFFIC) {  // tap a pilot in the list: fly to him / her
                ui::UiContext uc;
                uc.traffic = &traffic.list();
                int i = ui::trafficRowAt(uc, e.x, e.y);
                if (i >= 0) {
                    const Traffic& t = traffic.list()[i];
                    fc.setTargetPoint(t.name.empty() ? "ID " + fanetAddrStr(t.addr) : t.name, t.lat, t.lon);
                    fc.targetPointTag = t.addr;
                    screen = ui::SCR_NAV;
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
    {
        char b[96];
        snprintf(b, sizeof b, "%s%s%s", TR("rysowanie: ", "drawing: "),
                 menu.isOpen() ? menu.describe().c_str() : ui::screenName(screen), full ? TR(" (pełne)", " (full)") : "");
        diag::crumb(b);
    }
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
    u.traffic = &traffic.list();
    u.topoView = &topoView;
    u.lastLat = lastLat;
    u.lastLon = lastLon;
    if (powerUi != PowerUi::Normal) {
        if (powerUi == PowerUi::Ask) {
            uint32_t el = millis() - askSinceMs;
            int left = !askAuto ? -1 : (int)((kAskTimeoutMs - std::min(el, kAskTimeoutMs) + 999) / 1000);
            ui::drawUsbAsk(c, u, askFiles(), askSel, left, askDefault);
        } else {
            ui::drawCharging(c, u);
        }
        if (powerArming) ui::drawPowerHint(c);
        board::displayUpdate(full);
        return;
    }
    if (usbMode) {
        ui::drawUsbDisk(c, u, usbdisk::hostConnected());
        if (powerArming) ui::drawPowerHint(c);
        board::displayUpdate(full);
        return;
    }
    u.escape = &escapeRes;
    if (showRouteCheck) {
        ui::drawRouteCheck(c, u, route.name, routeCheck);
    } else if (menu.isOpen()) {
        menu.draw(c);
    } else if (flightViewOpen) {
        ui::drawFlightView(c, u, flightView);
    } else if (showSummary) {
        ui::drawSummary(c, u);
    } else if (escapeMode) {
        ui::drawEscape(c, u);
        int a = alerts.top();
        if (a >= 0 && a != AL_AIRSPACE_IN) ui::drawAlertBanner(c, u, a);
    } else {
        ui::drawScreen(c, screen, u);
        int a = alerts.top();
        if (a >= 0) ui::drawAlertBanner(c, u, a);
        else if (settings.trafficAlarm && screen != ui::SCR_TRAFFIC && fc.state().phase == Phase::Flying)
            ui::drawTrafficHint(c, u);  // caution level: small box, top right
    }
    if (fc.state().landingAsk) ui::drawLandingAsk(c, u);  // over any screen, menu included
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

// ------------------------------------------------------------------ power off
static void shutdownDevice() {
    ui::Canvas c(board::framebuffer());
    c.setFlip(settings.flip180);
    if (board::usbPowered()) {
        // BQ25896 cannot cut power while USB is connected
        ui::drawPowerOff(c, TR("Odłącz kabel USB", "Unplug the USB cable"), TR("przy zasilaniu z USB wyłączenie nie jest możliwe", "the device cannot power off while on USB power"));
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
    saveLastPos();
    if (sys.sdOk) diag::noteShutdown(PATH_LOGS);
    // the map tiles are read now, while the card is still mounted
    ui::drawPowerOff(c, TR("WYŁĄCZONY", "POWERED OFF"), TR("Włączanie: przycisk PWR lub podłączenie USB", "Power on: PWR button or USB"),
                     sys.sdOk && !diag::boot().safeMode ? &topo : nullptr, lastLat, lastLon);
    topo.close();
    SD.end();
    board::displayUpdate(true);  // e-paper keeps this image without power
    board::powerOff();
    // if we get here the power-off failed
    sys.sdOk = board::sdInit();
    ui::drawPowerOff(c, TR("Nie udało się wyłączyć", "Power off failed"), TR("spróbuj ponownie lub odłącz baterię", "try again or disconnect the battery"));
    board::displayUpdate(false);
    delay(3000);
}

// ------------------------------------------------------------------ setup / loop
void setup() {
    diag::begin();  // read what the previous run left in RTC memory before anything overwrites it
    Serial.begin(115200);
    // 100 kHz like the LilyGO factory firmware (epdiy configures the same bus to 100 kHz anyway)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    // fuel gauge first, before epdiy takes over the I2C port - same order as the factory firmware
    bool batt = board::batteryInit();
    board::backlight(0);

    // SD first so config.ini (VCOM, flip) is known before the display starts
    sys.sdOk = board::sdInit();
    if (sys.sdOk) settings.load(PATH_CONFIG);
    loadLastPos();
    applyLanguage();
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
        // tell the user if the previous run ended unexpectedly, and write the report to the card
        // right away - before loading data, so a crash while loading is reported too
        const diag::Boot& db = diag::boot();
        std::string report = sys.sdOk ? diag::writeReports(PATH_LOGS, PARANAV_VERSION) : "";
        Serial.printf("[boot] reset reason %d (%s), last action: %s\n", db.reason, diag::reasonText(db.reason),
                      db.lastAction.c_str());
        if (db.powerLoss) {
            boot.push_back(TR("[!!] Poprzednio: zanik zasilania bez wyłączenia (bateria?)", "[!!] Before: power lost without power-off (battery?)"));
            bootNotes.push_back(TR("Zanik zasilania bez wyłączenia (bateria?)", "Power lost without power-off (battery?)"));
            if (!report.empty()) boot.push_back(std::string(TR("[!!] Raport: logs/", "[!!] Report: logs/")) + report);
        } else if (db.unexpected) {
            boot.push_back(std::string(db.crash ? "[!!] " : "[..] ") + TR("Poprzedni restart: ", "Last restart: ") +
                           diag::reasonText(db.reason));
            if (!db.lastAction.empty()) boot.push_back(std::string(TR("[!!] Ostatnio: ", "[!!] Last action: ")) + db.lastAction);
            if (!report.empty()) boot.push_back(std::string(TR("[!!] Raport: logs/", "[!!] Report: logs/")) + report);
        }
        if (db.crash) {
            sys.lastCrash = std::string(diag::reasonText(db.reason)) + (db.lastAction.empty() ? "" : ": " + db.lastAction);
            bootNotes.push_back("Restart: " + sys.lastCrash);
            if (!report.empty()) bootNotes.push_back(std::string(TR("Raport: logs/", "Report: logs/")) + report);
        }
        if (db.safeMode) {
            boot.push_back(TR("[!!] TRYB AWARYJNY: Bluetooth, FANET i mapy wyłączone", "[!!] SAFE MODE: Bluetooth, FANET and maps off"));
            bootNotes.push_back(TR("TRYB AWARYJNY (3 restarty z rzędu): Bluetooth, FANET i mapy wył.",
                                        "SAFE MODE (3 restarts in a row): Bluetooth, FANET and maps off"));
        }
        sys.loadErrors = bootNotes;
        crumb("start");
    }
    boot.push_back(std::string(sys.sdOk ? "[OK] " : "[!!] ") + TR("Karta SD", "SD card") + (sys.sdOk ? "" : TR(" – brak, dane i zapis lotu niedostępne", " – missing, no data and no flight log")));
    bootScreen(boot);

    crumb(TR("start: barometr", "start: barometer"));
    sys.baroOk = baro.begin(Wire);
    {
        char b[80];
        if (sys.baroOk) snprintf(b, sizeof b, TR("[OK] Barometr na kablu MS5611 (adres 0x%02X)", "[OK] Wired barometer MS5611 (address 0x%02X)"), baro.address());
        else snprintf(b, sizeof b, TR("[--] Barometr na kablu: brak (użyję BlueFly albo GPS)", "[--] Wired barometer: none (BlueFly or GPS will be used)"));
        boot.push_back(b);
    }
    crumb(TR("start: skan I2C", "start: I2C scan"));
    sys.i2cDevices = board::i2cScan();
    Serial.printf("[i2c] %s\n", sys.i2cDevices.c_str());
    boot.push_back("[..] I2C: " + sys.i2cDevices);
    crumb(TR("start: bateria", "start: battery"));
    board::Battery bt0 = board::batteryRead();
    {
        char b[96];
        snprintf(b, sizeof b, TR("%s Bateria: %s %.2f V", "%s Battery: %s %.2f V"), batt && bt0.ok ? "[OK]" : "[!!]", bt0.source, bt0.volts);
        boot.push_back(b);
    }
    if (sys.sdOk) {
        crumb(TR("start: wczytywanie danych z SD", "start: loading data from SD"));
        loadData();
        char b[96];
        snprintf(b, sizeof b, TR("[OK] Strefy %d, punkty %d, trasa %d, teren %d, mapy %d", "[OK] Airspace %d, waypoints %d, route %d, terrain %d, maps %d"), sys.airspaceZones,
                 sys.waypoints, sys.routePoints, sys.terrainTiles, sys.topoAreas);
        boot.push_back(b);
        sys.sdFreeMb = board::sdFreeMb();
    }
    boot.push_back(TR("[..] GPS – wykrywanie modułu", "[..] GPS – detecting module"));
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
    if (!diag::boot().safeMode) {
        phone::setEnabled(settings.phoneEnabled);
        bluefly::begin(settings.blueflyEnabled);
    }
    xTaskCreatePinnedToCore(sensorTask, "sensors", 8192, nullptr, 5, nullptr, 1);
    // FANET: own 16-bit id, generated once and kept in config.ini
    if (settings.fanetId <= 0 || settings.fanetId >= 0xFFFF) {
        settings.fanetId = 1 + (int)(esp_random() % 0xFFFE);
        saveSettings();
    }
    if (!diag::boot().safeMode) {
        crumb("start: FANET");
        fanet::begin();
        applyFanetConfig();
    }
    delay(500);
    // powered on by plugging USB in (a normal start, not a crash restart): ask, default = charging
    usbPower = board::usbPowered();
    if (usbPower && !diag::boot().unexpected) askUsbMode(ui::UC_CHARGE);
    heap_caps_malloc_extmem_enable(kExtmemRun);  // from now on small temporary objects use internal RAM
    render(true);
    crumb(TR("praca", "running"));
}

void loop() {
    static uint32_t lastSecond = 0, lastRender = 0, lastFull = 0, lastBatt = 0;
    static uint32_t renderMaxMs = 0;  // slowest redraw since the last 10 s report
    static bool dirty = true;
    static bool userDirty = false;  // redraw caused by a button / touch
    uint32_t now = millis();

    board::Event e;
    while (board::inputGet(e))
        if (handleEvent(e)) dirty = userDirty = true;
    {
        PowerUi before = powerUi;
        bool wasUsb = usbMode;
        pollUsbPower(now);
        if (powerUi != before || usbMode != wasUsb) dirty = true;
    }
    {
        fanet::Rx r;
        FanetPacket p;
        while (fanet::getRx(r))
            if (fanetDecode(r.data, r.len, p)) traffic.onPacket(p, r.ms, r.rssi);
    }

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
            crumb(TR("instalacja pliku z telefonu: ", "installing file from phone: ") + p.kind);
            if (p.kind == "reload") {
                loadData();
                phone::notify("OK RELOADED");
                phone::setLastResult(TR("usunięto ", "deleted ") + p.name);
            } else {
                std::string msg;
                int count = 0;
                bool ok = nav::FileReceiver::install(SD_MOUNT, p, msg, count);
                if (ok) loadData();
                int conflicts = -1;
                if (ok && p.kind == "route" && p.activate) {  // "set as active route" in the app
                    selectRoute(std::string(SD_MOUNT) + "/" + msg);
                    if (fc.state().phase != Phase::Flying) fc.setTargetRoute(0);
                    conflicts = checkActiveRoute(fc.state().phase != Phase::Flying);
                }
                const char* what[] = {"airspace", TR("stref", "airspaces"), "activations", TR("aktywacji", "activations"), "waypoints", TR("punktów", "waypoints"),
                                             "route", TR("punktów trasy", "route points")};
                const char* unit = "";
                for (int i = 0; i < 8; i += 2)
                    if (p.kind == what[i]) unit = what[i + 1];
                phone::setLastResult(ok ? std::to_string(count) + " " + unit + " – " + msg : TR("BŁĄD: ", "ERROR: ") + msg);
                // " AIRSPACE <n>": zones crossed by the activated route (the app shows a warning)
                phone::notify(ok ? "OK END " + std::to_string(count) + " " + msg +
                                       (conflicts >= 0 ? " AIRSPACE " + std::to_string(conflicts) : std::string())
                                 : "ERR " + msg);
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
        sys.chgStat = bt.chgStat;
        sys.battMinToFull = bt.minutesToFull;
        sys.battRemMah = bt.remMah;
        sys.battFullMah = bt.fullMah;
        sys.freeHeapKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
        sys.freePsramKb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        {
            multi_heap_info_t hp, hi;
            heap_caps_get_info(&hp, MALLOC_CAP_SPIRAM);
            heap_caps_get_info(&hi, MALLOC_CAP_INTERNAL);
            sys.psramFreeBlocks = hp.free_blocks;
            sys.renderMaxMs = renderMaxMs;
            Serial.printf("[heap] up %us  PSRAM free %u kB, free blocks %u, used blocks %u, largest %u kB | "
                          "internal free %u kB, free blocks %u, largest %u kB | render max %u ms\n",
                          (unsigned)(now / 1000), (unsigned)(hp.total_free_bytes / 1024), (unsigned)hp.free_blocks,
                          (unsigned)hp.allocated_blocks, (unsigned)(hp.largest_free_block / 1024),
                          (unsigned)(hi.total_free_bytes / 1024), (unsigned)hi.free_blocks,
                          (unsigned)(hi.largest_free_block / 1024), (unsigned)renderMaxMs);
            renderMaxMs = 0;
        }
        sys.gpsSentences = gps.sentences();
        sys.gpsCsErr = gps.checksumErrors();
        board::lastTouch(sys.touchX, sys.touchY);
        sys.baroOk = baro.ok();
        sys.baroBlanked = baroBlanked;
        sys.baroSpikes = baroSpikes;
        lastBatt = now;
    }
    {
        // once per second: state snapshot in RTC memory for the restart report (diag.h)
        static uint32_t lastDiag = 0, n = 0;
        static bool usb = false;
        if (now - lastDiag >= 1000) {
            usb = usbPower;  // polled once per second by pollUsbPower()
            n++;
            diag::Snapshot d;
            d.uptimeS = now / 1000;
            d.battV = sys.battV;
            d.battPct = sys.battPct;
            d.usb = usb;
            d.flying = fc.state().phase == Phase::Flying;
            snprintf(d.screen, sizeof d.screen, "%s", powerUi == PowerUi::Charging ? "CHARGING" : powerUi == PowerUi::Ask ? "USB?" : usbMode ? "USB" : menu.isOpen() ? "MENU" : ui::screenName(screen));
            d.fanetOn = sys.fanetState == 1 || sys.fanetState == 2;
            d.fanetTx = sys.fanetTxCount;
            diag::tick(d);
            lastDiag = now;
        }
    }
    // Menu, help and the flight summary are static: they are redrawn only after a button / touch
    // (no 1 s updates, no timed full refresh). A full refresh is done when such a view opens or
    // closes and after every 20 quick updates inside it (ghosting of the highlighted row).
    const int view = powerUi == PowerUi::Ask        ? 4
                     : powerUi == PowerUi::Charging ? 5
                     : usbMode                      ? 3
                     : showRouteCheck               ? 6
                     : menu.isOpen()                ? 1
                     : showSummary                  ? 2
                     : escapeMode                   ? 7
                     : flightViewOpen               ? 8
                                                    : 0;
    static int lastView = -1, quickInView = 0;
    if (view == 3 && lastView != 3 && lastView >= 0) {  // USB drive: already drawn by usbEnter()
        lastView = 3;
        dirty = userDirty = false;
    }
    if (view == 1 || view == 2 || view == 6 || view == 8) {
        if (!userDirty && view == lastView) dirty = false;
    }
    if (view == 4 && !askAuto && !userDirty && view == lastView) dirty = false;  // no countdown: static
    if (view == 5) {  // charging screen: when the percentage / charger state changes, numbers once a minute
        static int lastKey = -1;
        static uint32_t lastNumbers = 0;
        int key = sys.battPct * 16 + (sys.charging ? 8 : 0) + (sys.chgStat & 7);
        bool numbers = now - lastNumbers >= 60000;
        if (!userDirty && view == lastView && key == lastKey && !numbers) dirty = false;
        if (dirty) lastNumbers = now;
        lastKey = key;
    }
    if (dirty && now - lastRender >= 250) {
        bool full;
        if (view != lastView && lastView >= 0) full = true;
        else if (view == 1 || view == 2 || view == 4) full = quickInView >= 20;
        else if (view == 8) full = quickInView >= 5;  // flight viewer: whole map changes, ghosting builds up fast
        else if (view == 5) full = quickInView >= 10;
        else full = settings.fullRefreshS > 0 && now - lastFull > (uint32_t)settings.fullRefreshS * 1000;
        uint32_t t0 = millis();
        render(full);
        if (!full) renderMaxMs = std::max(renderMaxMs, (uint32_t)(millis() - t0));  // full refresh is slow by design
        quickInView = full ? 0 : quickInView + 1;
        if (full) lastFull = now;
        lastView = view;
        lastRender = now;
        dirty = userDirty = false;
    }
    delay(10);
}
