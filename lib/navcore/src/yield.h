// Cooperative yield for long-running loaders (big airspace / waypoint files, terrain index).
// On the device this lets the FreeRTOS idle task run, so the task watchdog (5 s) does not fire
// while a large file is parsed. On the desktop it does nothing.
#pragma once

namespace nav {

using YieldFn = void (*)();
inline YieldFn gYield = nullptr;
inline void setYield(YieldFn f) { gYield = f; }

// Call often; actually yields every `every` calls.
inline void maybeYield(unsigned every = 200) {
    static unsigned n = 0;
    if (gYield && ++n >= every) {
        n = 0;
        gYield();
    }
}

}  // namespace nav
