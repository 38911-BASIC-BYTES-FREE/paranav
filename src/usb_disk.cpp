#include "usb_disk.h"
#include "i18n.h"

#include <Arduino.h>
#include <SD.h>

#if !ARDUINO_USB_MODE && CONFIG_TINYUSB_MSC_ENABLED
#include "USB.h"
#include "USBMSC.h"
#include "tusb.h"

namespace usbdisk {

static USBMSC msc;  // global: its constructor registers the MSC interface before USB starts
static volatile bool sActive = false, sEjected = false, sInit = false;
static const char* sErr = "";

static int32_t onRead(uint32_t lba, uint32_t offset, void* buf, uint32_t size) {
    if (!sActive || offset != 0 || (size % 512)) return -1;
    uint8_t* p = (uint8_t*)buf;
    for (uint32_t i = 0; i < size / 512; i++)
        if (!SD.readRAW(p + i * 512, lba + i)) return -1;
    return (int32_t)size;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t size) {
    if (!sActive || offset != 0 || (size % 512)) return -1;
    for (uint32_t i = 0; i < size / 512; i++)
        if (!SD.writeRAW(buf + i * 512, lba + i)) return -1;
    return (int32_t)size;
}

static bool onStartStop(uint8_t, bool start, bool loadEject) {
    if (loadEject && !start) {  // "Eject" in the PC
        msc.mediaPresent(false);
        sEjected = true;
    }
    return true;
}

bool supported() { return true; }

// The drive is registered at boot without media ("card reader without a card"). When the card is
// inserted later, the Arduino MSC callbacks never report "medium changed", so Windows keeps its
// cached empty drive and nothing appears. A short USB disconnect makes the computer enumerate the
// device again and read the card from scratch (the serial port reconnects as well).
static void reconnect() {
    if (!tud_connected()) return;  // no computer: nothing to refresh
    tud_disconnect();
    delay(400);
    tud_connect();
}

void init() {
    msc.vendorID("ParaNav");
    msc.productID("Karta SD");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(false);
    sInit = true;
}

bool start() {
    if (!sInit) init();
    size_t n = SD.numSectors(), sz = SD.sectorSize();
    if (!n || sz != 512) {
        sErr = TR("brak karty SD", "no SD card");
        return false;
    }
    if (!msc.begin((uint32_t)n, (uint16_t)sz)) {
        sErr = TR("USB: błąd startu", "USB: start error");
        return false;
    }
    sEjected = false;
    sActive = true;
    msc.mediaPresent(true);
    reconnect();
    return true;
}

void stop() {
    msc.mediaPresent(false);
    delay(50);  // let a transfer in progress finish
    sActive = false;
    reconnect();
}

bool active() { return sActive; }
bool hostConnected() { return (bool)USB; }
bool takeEjected() {
    bool e = sEjected;
    sEjected = false;
    return e;
}
const char* lastError() { return sErr; }

}  // namespace usbdisk

#else  // hardware CDC/JTAG USB mode: no mass storage

namespace usbdisk {
bool supported() { return false; }
void init() {}
bool start() { return false; }
void stop() {}
bool active() { return false; }
bool hostConnected() { return false; }
bool takeEjected() { return false; }
const char* lastError() { return TR("niedostępne (USB Mode = hwcdc)", "not available (USB Mode = hwcdc)"); }
}  // namespace usbdisk

#endif
