// FANET radio: SX1262 LoRa module on the board (868.2 MHz). Runs in its own task: listens all the
// time, transmits our position every 5 s (FANET rule) and hands received packets to loop().
#pragma once
#include <cstdint>
#include <string>

#include "fanet.h"

namespace fanet {

struct Config {
    bool rx = true;        // radio on
    bool tx = true;        // transmit own position
    uint32_t addr = 0;     // manufacturer << 16 | id
    std::string name;      // pilot name (FANET "name" packet every 4 min)
};

struct OwnTx {
    bool valid = false;    // fresh GPS fix
    bool flying = false;   // tracking packet in the air, ground tracking on the ground
    nav::FanetOwn pos;
    int neighbours = 0;    // aircraft heard recently (longer interval in crowds)
};

struct Rx {
    uint8_t len = 0;
    uint8_t data[nav::kFanetMaxPacket];
    int16_t rssi = 0;
    uint32_t ms = 0;
};

struct Status {
    int state = 0;         // 0 off, 1 receiving, 2 receiving + transmitting, 3 error
    std::string err;
    uint32_t tx = 0, rx = 0, rxErr = 0;
    int rssi = 0;
};

void begin();
void configure(const Config& c);
void setOwn(const OwnTx& o);
bool getRx(Rx& r);
Status status();

}  // namespace fanet
