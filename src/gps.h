// GNSS module detection/configuration (u-blox MIA-M10Q or Quectel L76K) on Serial1.
#pragma once
#include <Arduino.h>

#include "nmea.h"

class GpsModule {
public:
    // Detect baud rate/module and configure airborne dynamic model. Returns true if NMEA seen.
    bool begin();
    // Read all pending bytes; returns true when a new epoch was parsed.
    bool poll();
    const nav::GpsFix& fix() const { return parser_.fix(); }
    const char* moduleName() const { return name_; }
    uint32_t sentences() const { return parser_.sentences(); }
    uint32_t checksumErrors() const { return parser_.checksumErrors(); }

private:
    bool listen(uint32_t baud, uint32_t ms);
    void sendUbxValset8(uint32_t key, uint8_t val);
    void sendNmea(const char* body);
    nav::NmeaParser parser_;
    const char* name_ = "–";
};
