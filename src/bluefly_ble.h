// BlueFlyVario over Bluetooth Low Energy (v25: Microchip RNBD350, "Transparent UART" service).
// A background task scans for a device named "BlueFly..." (or advertising the service),
// connects, subscribes to the data characteristic and reconnects automatically.
#pragma once
#include <Arduino.h>

#include <string>

#include "bluefly.h"

namespace bluefly {

enum class State : uint8_t { Off, Scanning, Connecting, Connected, Error };

struct Sample {
    nav::BlueFlyParser::Kind kind;
    float value;     // hPa (Pressure) or metres (Altitude)
    uint32_t micros;
};

struct Status {
    State state = State::Off;
    std::string name;        // e.g. "BlueFly-7970"
    int rssi = 0;
    int batteryPct = -1;
    float batteryV = 0;
    float tempC = NAN;
    const char* format = "?";
    uint32_t lines = 0;
    uint32_t lastDataMs = 0;  // millis() of the last sample
    std::string error;        // State::Error: why Bluetooth is not running
};

void begin(bool enabled);
void setEnabled(bool on);
void reconnect();                  // drop the current connection and search again
void retryStack();                 // clear a Bluetooth start error and try again
bool getSample(Sample& s);         // non-blocking, called by the sensor task
Status status();

}  // namespace bluefly
