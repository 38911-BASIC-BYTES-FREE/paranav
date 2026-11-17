// Hardware abstraction for LilyGO T5 E-Paper S3 Pro: e-paper, buttons, touch, battery, SD.
#pragma once
#include <Arduino.h>

#include <string>

namespace board {

// ---- e-paper
bool displayInit(int vcomMv);
uint8_t* framebuffer();                 // 960x540, 4 bpp, epdiy layout
void displayUpdate(bool full);          // full = flashing GC16 refresh (clears ghosting)
int displayTemperature();

// ---- buttons & touch (polled in an input task -> event queue)
enum class Ev : uint8_t {
    None, KeyShort, KeyLong, BootShort, BootLong, Tap,
    PowerArm,     // both buttons held for 1 s  -> show "hold to power off"
    PowerCancel,  // released before 3 s
    PowerOff,     // both buttons held for 3 s
    BothShort     // both buttons pressed together and released before 1 s -> backlight toggle
};
struct Event {
    Ev type = Ev::None;
    int16_t x = 0, y = 0;  // for Tap, in logical screen coordinates
};
void inputStart(bool flip180);
void inputSetFlip(bool flip180);
bool inputGet(Event& e);                 // non-blocking
void lastTouch(int& x, int& y);

// ---- battery (BQ27220 fuel gauge)
struct Battery {
    bool ok = false;
    int pct = -1;
    float volts = 0;
    int ma = 0;
    int minutesLeft = -1;
    bool charging = false;
    const char* source = "–";
};
bool batteryInit();
Battery batteryRead();

// ---- power (BQ25896 charger)
bool usbPowered();   // VBUS present -> software power-off not possible
bool powerOff();     // BATFET off (ship mode). Wake: PWR/QON button or plugging USB. false if USB present.

// ---- front light (PT4103 LED driver, PWM on GPIO11, <= 1 kHz)
void backlight(int percent);  // 0 = off

// ---- diagnostics
std::string i2cScan();  // e.g. "20 51 55 5D 68 6B 77"

// ---- SD card (mounted at /sd for stdio access)
bool sdInit();
float sdFreeMb();

}  // namespace board
