// Crash / restart diagnostics.
// A small block of RTC memory survives software resets, panics and watchdog resets. It keeps the
// last actions ("breadcrumbs") and a once-per-second snapshot (battery, memory, screen...). At the
// next boot every unexpected restart is written to the SD card:
//   /logs/restarts.txt   one line per boot (also normal ones), with the reset reason
//   /logs/crash-NN.txt   full report for every unexpected restart (with the core dump backtrace
//                        when the ESP-IDF saved one - panics and watchdogs; brownouts have none)
// After 3 unexpected restarts in a row within 2 minutes of running, the device starts in "safe
// mode" (Bluetooth, FANET radio and TOPO maps off) so that it stays usable and the report can be read.
#pragma once
#include <cstdint>
#include <string>

namespace diag {

struct Snapshot {
    uint32_t uptimeS = 0;
    float battV = 0;
    int battPct = -1;
    bool usb = false, flying = false, fanetOn = false;
    char screen[16] = "";  // screen name, MENU or USB
    uint32_t fanetTx = 0;
};

struct Boot {
    int reason = 0;            // esp_reset_reason_t
    bool unexpected = false;   // panic, watchdog, brownout, software restart
    bool crash = false;        // panic / watchdog / brownout (shown on the boot screen)
    bool safeMode = false;
    uint32_t bootCount = 0;
    std::string lastAction;    // last breadcrumb of the previous run
    uint32_t prevUptimeS = 0;
    bool powerLoss = false;    // power-on reset, but the previous run did not power off properly
};

// Call first thing in setup(): reads what the previous run left in RTC memory.
const Boot& begin();
const Boot& boot();
const char* reasonText(int reason);  // in the current UI language
// Breadcrumb: what the device is doing now (cheap, call often).
void crumb(const char* s);
// Once per second from loop().
void tick(const Snapshot& s);
// Write /logs/restarts.txt + /logs/crash-NN.txt (SD must be mounted). Returns "crash-03.txt" or "".
std::string writeReports(const char* logsDir, const char* version);
// Power off from the menu / buttons: noted in restarts.txt, so that a later power-on is not
// reported as a power loss.
void noteShutdown(const char* logsDir);

}  // namespace diag
