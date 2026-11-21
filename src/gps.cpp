#include "gps.h"
#include "i18n.h"

#include "config.h"

bool GpsModule::listen(uint32_t baud, uint32_t ms) {
    Serial1.end();
    Serial1.setRxBufferSize(2048);
    Serial1.begin(baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    uint32_t t0 = millis();
    uint32_t before = parser_.sentences();
    while (millis() - t0 < ms) {
        while (Serial1.available()) parser_.feed((char)Serial1.read());
        if (parser_.sentences() - before >= 3) return true;
        delay(5);
    }
    return false;
}

void GpsModule::sendUbxValset8(uint32_t key, uint8_t val) {
    // UBX-CFG-VALSET (0x06 0x8A), RAM layer only
    uint8_t msg[6 + 9 + 2] = {0xB5, 0x62, 0x06, 0x8A, 9, 0, 0x00, 0x01, 0x00, 0x00,
                              (uint8_t)key, (uint8_t)(key >> 8), (uint8_t)(key >> 16), (uint8_t)(key >> 24), val};
    uint8_t a = 0, b = 0;
    for (int i = 2; i < 6 + 9; i++) {
        a += msg[i];
        b += a;
    }
    msg[15] = a;
    msg[16] = b;
    Serial1.write(msg, sizeof msg);
}

void GpsModule::sendNmea(const char* body) {
    uint8_t cs = 0;
    for (const char* p = body; *p; ++p) cs ^= (uint8_t)*p;
    char buf[96];
    snprintf(buf, sizeof buf, "$%s*%02X\r\n", body, cs);
    Serial1.print(buf);
}

bool GpsModule::begin() {
    // u-blox M10 default is 38400, L76K 9600 (see LilyGO pinmap.md)
    if (listen(38400, 2500)) {
        name_ = "u-blox MIA-M10Q";
        sendUbxValset8(0x20110021, 6);  // CFG-NAVSPG-DYNMODEL = 6 (airborne < 1 g)
        return true;
    }
    if (listen(9600, 2500)) {
        name_ = "Quectel L76K";
        sendNmea("PCAS04,7");   // GPS + BeiDou + GLONASS
        delay(100);
        sendNmea("PCAS11,5");   // dynamic model: airborne < 1 g
        delay(100);
        sendNmea("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");  // only GGA + RMC
        return true;
    }
    name_ = TR("brak odpowiedzi", "no response");
    // keep listening at 38400 - the module might start later
    Serial1.end();
    Serial1.begin(38400, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    return false;
}

bool GpsModule::poll() {
    bool epoch = false;
    while (Serial1.available()) {
        if (parser_.feed((char)Serial1.read())) epoch = true;
    }
    return epoch;
}
