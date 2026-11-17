// "USB mode": the SD card is shown to a PC as a USB drive (mass storage), so maps and other data
// can be copied with a cable instead of taking the card out. Needs USB-OTG (TinyUSB) build mode.
#pragma once

namespace usbdisk {

bool supported();        // false when built with the hardware CDC/JTAG USB mode
void init();             // call once after the SD card was mounted (registers the drive, no media yet)
bool start();            // hand the card over to the PC (caller must stop all SD file access first)
void stop();             // take the card back (caller remounts the file system afterwards)
bool active();
bool hostConnected();    // PC has enumerated the device
bool takeEjected();      // PC ejected the drive since the last call
const char* lastError();

}  // namespace usbdisk
