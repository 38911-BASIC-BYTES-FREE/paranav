// "ParaNav" BLE peripheral: lets the Android app upload airspace / activations / waypoints / route files.
#pragma once
#include <string>

#include "filexfer.h"

namespace phone {

// UUIDs shared with the Android app (app/src/main/java/.../Proto.java)
static constexpr const char* kSvcUuid = "7e2a0001-5a1d-4c3b-9f0e-3a8d2c6b1e01";
static constexpr const char* kCtrlUuid = "7e2a0002-5a1d-4c3b-9f0e-3a8d2c6b1e01";  // write + notify (text)
static constexpr const char* kDataUuid = "7e2a0003-5a1d-4c3b-9f0e-3a8d2c6b1e01";  // write without response

struct Status {
    bool enabled = false;
    bool connected = false;
    bool receiving = false;
    std::string kind, name;
    float progress = 0;
    std::string last;  // result of the last upload, shown on the SYSTEM screen
};

void setEnabled(bool on);
bool enabled();
void setupServer();          // called by the BLE task once the stack is running
void tick();                 // called periodically by the BLE task (advertising on/off)
bool busy();                 // transfer in progress (BlueFly scanning is paused meanwhile)
Status status();
bool takePending(nav::FileReceiver::Pending& p);
void notify(const std::string& s);
void setLastResult(const std::string& s);

}  // namespace phone
