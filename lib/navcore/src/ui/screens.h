// Screen renderers. Everything is drawn from a read-only UiContext snapshot,
// so the same code runs on the device and in the desktop preview tool.
#pragma once
#include <string>
#include <vector>

#include "../airspace.h"
#include "../alerts.h"
#include "../flight.h"
#include "../settings.h"
#include "../terrain.h"
#include "../topo.h"
#include "../waypoints.h"
#include "canvas.h"

namespace ui {

enum ScreenId { SCR_FLIGHT = 0, SCR_MAP, SCR_TOPO, SCR_NAV, SCR_THERMAL, SCR_AIRSPACE, SCR_FUEL, SCR_SYSTEM, SCR_COUNT };
const char* screenName(ScreenId s);

struct SysInfo {
    int battPct = -1;
    float battV = 0;
    int battMa = 0;          // + charging, - discharging
    int battMinLeft = -1;
    bool charging = false;
    std::string battSource = "–";
    std::string i2cDevices;
    bool backlight = false;
    std::string lastCrash;
    // BlueFly (Bluetooth)
    int bfState = 0;          // 0 off, 1 scanning, 2 connecting, 3 connected, 4 error
    std::string bfError;
    std::string bfName;
    int bfRssi = 0, bfBattPct = -1;
    float bfBattV = 0;
    std::string bfFormat;
    // phone link (Android app)
    int phoneState = 0;       // 0 off, 1 waiting, 2 connected, 3 receiving
    float phoneProgress = 0;
    std::string phoneFile, phoneLast;
    bool sdOk = false;
    float sdFreeMb = 0;
    int airspaceZones = 0, airspacePoints = 0, activationEntries = -1;
    uint32_t activationDate = 0;
    int waypoints = 0, routePoints = 0, terrainTiles = 0;
    int topoAreas = 0;            // background map areas on the SD card (/maps)
    std::string topoArea;         // area shown on the TOPO screen
    bool usbDisk = false;         // SD card handed over to the PC (USB mass storage)
    std::string gpsModule = "?";
    bool baroOk = false;
    bool recording = false;
    std::string logFile;
    std::string version = "0.1";
    int touchX = -1, touchY = -1;
    uint32_t freeHeapKb = 0, freePsramKb = 0;
    uint32_t gpsSentences = 0, gpsCsErr = 0;
    std::vector<std::string> loadErrors;
};

struct UiContext {
    const nav::NavState* nav = nullptr;
    const nav::Settings* set = nullptr;
    const nav::AirspaceDb* asp = nullptr;
    const std::vector<nav::AirspaceHit>* hits = nullptr;
    const nav::WaypointDb* wpts = nullptr;
    const nav::Route* route = nullptr;
    const nav::FlightComputer* fc = nullptr;
    nav::Terrain* terrain = nullptr;
    nav::TopoMap* topo = nullptr;     // background map tiles (may be null)
    nav::TopoView* topoView = nullptr;  // TOPO screen view state (kept between frames)
    const nav::AlertManager* alerts = nullptr;
    const SysInfo* sys = nullptr;
};

void drawScreen(Canvas& c, ScreenId s, const UiContext& u);
void drawStatusBar(Canvas& c, ScreenId s, const UiContext& u, const char* titleOverride = nullptr);
void drawAlertBanner(Canvas& c, const UiContext& u, int alertId);
void drawSummary(Canvas& c, const UiContext& u);
void drawPowerHint(Canvas& c);
void drawTransferBanner(Canvas& c, const UiContext& u);  // file upload from the phone in progress  // "keep holding to power off" overlay
void drawPowerOff(Canvas& c, const char* line1, const char* line2);
void drawBoot(Canvas& c, const std::vector<std::string>& lines, const char* version);

// Touch zones of the fuel screen buttons (for the input handler)
enum FuelButton { FB_NONE = -1, FB_MINUS = 0, FB_PLUS, FB_FULL, FB_ENGINE };
FuelButton fuelButtonAt(int x, int y);
// TOPO screen: touch zone -> +1 zoom in, -1 zoom out, 0 = not a zoom zone
int topoZoomTapAt(int x, int y);
// USB disk mode screen
void drawUsbDisk(Canvas& c, const UiContext& u, bool hostConnected);

// Formatting helpers
std::string fmtInt(float v, const char* invalid = "––");
std::string fmtSigned1(float v);
std::string fmtDistKm(float m);
std::string fmtDur(float sec, bool hm = true);
std::string fmtClock(uint32_t secOfDay);

}  // namespace ui
