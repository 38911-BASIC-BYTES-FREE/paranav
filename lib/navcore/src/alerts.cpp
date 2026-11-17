#include "alerts.h"

namespace nav {

bool AlertManager::set(AlertId id, bool cond, uint32_t now, const std::string& title, const std::string& detail,
                       int key) {
    Alert& a = a_[id];
    if (!cond) {
        a.active = false;
        a.acked = false;
        a.key = -1;
        return false;
    }
    bool fired = false;
    if (!a.active || a.key != key) {
        a.active = true;
        a.acked = false;
        a.since = now;
        fired = true;
    }
    a.key = key;
    a.title = title;
    a.detail = detail;
    // critical alerts come back after rearmS even if acknowledged
    if (a.acked && (id == AL_AIRSPACE_IN || id == AL_FUEL_RETURN) && now - a.ackedAt >= rearmS) {
        a.acked = false;
        fired = true;
    }
    return fired;
}

int AlertManager::top() const {
    for (int i = 0; i < AL_COUNT; i++)
        if (a_[i].active && !a_[i].acked) return i;
    return -1;
}

int AlertManager::topAny() const {
    for (int i = 0; i < AL_COUNT; i++)
        if (a_[i].active) return i;
    return -1;
}

void AlertManager::ack(int id, uint32_t now) {
    if (id < 0 || id >= AL_COUNT) return;
    a_[id].acked = true;
    a_[id].ackedAt = now;
}

}  // namespace nav
