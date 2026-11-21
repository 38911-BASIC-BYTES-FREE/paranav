#include "board.h"
#include "i18n.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <epdiy.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

#include "TouchDrvGT911.hpp"
#include "bq27220.h"
#include "config.h"

namespace board {

// ------------------------------------------------------------------ e-paper
static EpdiyHighlevelState hl;
static bool epdOk = false;
static int epdTemp = 20;
static uint32_t lastTempRead = 0;

bool displayInit(int vcomMv) {
    // Same sequence as the LilyGO factory firmware: Wire first, then epdiy (V7 board = PCA9535 + TPS65185).
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(vcomMv);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_set_rotation(EPD_ROT_LANDSCAPE);
    epd_set_lcd_pixel_clock_MHz(17);
    epd_poweron();
    epdTemp = epd_ambient_temperature();
    epd_clear();
    epd_poweroff();
    epdOk = true;
    return true;
}

uint8_t* framebuffer() { return epd_hl_get_framebuffer(&hl); }

int displayTemperature() { return epdTemp; }

static volatile bool fullRefreshActive = false;
static volatile uint32_t blankUntilMs = 0;

bool baroBlanked() { return fullRefreshActive || (int32_t)(millis() - blankUntilMs) < 0; }

void displayUpdate(bool full) {
    if (!epdOk) return;
    if (full) fullRefreshActive = true;
    epd_poweron();
    if (millis() - lastTempRead > 60000 || lastTempRead == 0) {
        epdTemp = epd_ambient_temperature();
        lastTempRead = millis();
    }
    if (full) {
        // keep the new drawing, flash the panel clean (removes ghosting), then draw it in 16 grey levels
        static uint8_t* tmp = nullptr;
        const size_t sz = 960 * 540 / 2;
        if (!tmp) tmp = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (tmp) {
            memcpy(tmp, framebuffer(), sz);
            // same as epd_fullclear() but without its assert(): a failed refresh must not reboot the device
            epd_hl_set_all_white(&hl);
            enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, epdTemp);
            if (err != EPD_DRAW_SUCCESS) Serial.printf("[epd] clear error 0x%x\n", (unsigned)err);
            epd_clear();
            memcpy(framebuffer(), tmp, sz);
        }
        epd_hl_update_screen(&hl, MODE_GC16, epdTemp);
    } else {
        epd_hl_update_screen(&hl, MODE_DU, epdTemp);
    }
    epd_poweroff();
    if (full) {
        blankUntilMs = millis() + 1000;  // let the supply settle after the panel is switched off
        fullRefreshActive = false;
    }
}

// ------------------------------------------------------------------ input
static QueueHandle_t evQueue = nullptr;
static TouchDrvGT911 touch;
static bool touchOk = false;
static volatile bool flip = false;
static volatile int lastTx = -1, lastTy = -1;

static bool readPcaButton() {
    Wire.beginTransmission(PCA9535_ADDR);
    Wire.write(0x00 + PCA9535_BTN_PORT);  // input port register 0/1
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(PCA9535_ADDR, 1) != 1) return false;
    return (Wire.read() & PCA9535_BTN_MASK) == 0;  // active low
}

static void toScreen(int16_t rx, int16_t ry, int16_t& x, int16_t& y) {
#if TOUCH_RAW_PORTRAIT
    // raw is portrait 540x960 matching epdiy EPD_ROT_INVERTED_PORTRAIT: native = (ry, 539 - rx)
    int nx = ry, ny = 539 - rx;
#else
    int nx = rx, ny = ry;
#endif
    if (flip) {
        nx = 959 - nx;
        ny = 539 - ny;
    }
    x = (int16_t)constrain(nx, 0, 959);
    y = (int16_t)constrain(ny, 0, 539);
}

struct BtnState {
    bool down = false;
    bool longSent = false;
    bool suppressed = false;  // part of the two-button combo: no short/long event
    uint32_t t0 = 0;
};

static void send(Ev t) {
    Event e;
    e.type = t;
    xQueueSend(evQueue, &e, 0);
}

static void handleBtn(BtnState& s, bool pressed, Ev shortEv, Ev longEv) {
    uint32_t now = millis();
    if (pressed && !s.down) {
        s.down = true;
        s.longSent = false;
        s.suppressed = false;
        s.t0 = now;
    } else if (pressed && s.down && !s.longSent && !s.suppressed && now - s.t0 >= LONG_PRESS_MS) {
        send(longEv);
        s.longSent = true;
    } else if (!pressed && s.down) {
        s.down = false;
        if (!s.longSent && !s.suppressed && now - s.t0 >= 30) send(shortEv);
    }
}

static void inputTask(void*) {
    BtnState key, boot;
    bool touching = false;
    uint32_t lastTouchPoll = 0;
    // two-button combo state
    bool combo = false, armed = false, offSent = false;
    uint32_t comboT0 = 0;
    for (;;) {
        bool k = readPcaButton();
        bool b = digitalRead(PIN_BOOT_BTN) == LOW;
        handleBtn(key, k, Ev::KeyShort, Ev::KeyLong);
        handleBtn(boot, b, Ev::BootShort, Ev::BootLong);

        uint32_t now = millis();
        if (k && b) {
            if (!combo) {
                combo = true;
                armed = offSent = false;
                comboT0 = now;
            }
            // while both are held, neither button produces its own events
            key.suppressed = boot.suppressed = true;
            if (!armed && now - comboT0 >= POWER_ARM_MS) {
                send(Ev::PowerArm);
                armed = true;
            }
            if (!offSent && now - comboT0 >= POWER_HOLD_MS) {
                send(Ev::PowerOff);
                offSent = true;
            }
        } else if (combo) {
            if (armed && !offSent) send(Ev::PowerCancel);
            if (!armed) send(Ev::BothShort);
            combo = false;
            // the button still held keeps 'suppressed' until it is released
        }

        if (touchOk && now - lastTouchPoll >= 40) {
            lastTouchPoll = now;
            int16_t tx, ty;
            bool t = touch.isPressed() && touch.getPoint(&tx, &ty, 1) > 0;
            if (t && !touching) {
                Event e;
                e.type = Ev::Tap;
                toScreen(tx, ty, e.x, e.y);
                lastTx = e.x;
                lastTy = e.y;
                xQueueSend(evQueue, &e, 0);
            }
            touching = t;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void inputStart(bool flip180) {
    flip = flip180;
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    evQueue = xQueueCreate(16, sizeof(Event));
    touch.setPins(PIN_TOUCH_RST, PIN_TOUCH_INT);
    touchOk = touch.begin(Wire, GT911_SLAVE_ADDRESS_L, PIN_I2C_SDA, PIN_I2C_SCL);
    if (touchOk) touch.setInterruptMode(LOW_LEVEL_QUERY);
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 3, nullptr, 1);
}

void inputSetFlip(bool f) { flip = f; }

bool inputGet(Event& e) { return evQueue && xQueueReceive(evQueue, &e, 0) == pdTRUE; }

void lastTouch(int& x, int& y) {
    x = lastTx;
    y = lastTy;
}

// ------------------------------------------------------------------ battery
// Primary source: BQ27220 fuel gauge (0x55), read with our own error-checked I2C reads
// (the bundled library ignores I2C errors and its init() result is not needed for the
// standard commands). Fallback: BQ25896 charger ADC (0x6B) battery voltage + LiPo curve.
static BQ27220 gauge;
static constexpr uint8_t BQ27220_ADDR = 0x55;
static constexpr uint8_t BQ25896_ADDR = 0x6B;
static bool chargerAdcOn = false;

static bool i2cRead(uint8_t dev, uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(dev);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(dev, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

static bool i2cWrite8(uint8_t dev, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(dev);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

static bool gaugeWord(uint8_t cmd, uint16_t& v) {
    uint8_t b[2];
    if (!i2cRead(BQ27220_ADDR, cmd, b, 2)) return false;
    v = (uint16_t)(b[0] | (b[1] << 8));  // little endian
    return true;
}

static int lipoPercent(float v) {
    static const float tv[] = {3.30f, 3.50f, 3.60f, 3.65f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
    static const int tp[] = {0, 5, 12, 20, 30, 40, 50, 65, 80, 90, 100};
    if (v <= tv[0]) return 0;
    for (int i = 1; i < 11; i++)
        if (v <= tv[i]) return tp[i - 1] + (int)((v - tv[i - 1]) / (tv[i] - tv[i - 1]) * (tp[i] - tp[i - 1]));
    return 100;
}

static bool chargerBattery(float& volts, bool& charging) {
    uint8_t r;
    if (!chargerAdcOn) {
        // REG02: CONV_START (bit7) + CONV_RATE (bit6) = continuous ADC, 1 s
        if (!i2cRead(BQ25896_ADDR, 0x02, &r, 1)) return false;
        i2cWrite8(BQ25896_ADDR, 0x02, r | 0xC0);
        chargerAdcOn = true;
        delay(20);
    }
    if (!i2cRead(BQ25896_ADDR, 0x0E, &r, 1)) return false;  // REG0E BATV
    volts = (2304 + (r & 0x7F) * 20) / 1000.0f;
    uint8_t st;
    charging = i2cRead(BQ25896_ADDR, 0x0B, &st, 1) && ((st >> 3) & 0x03) != 0 && ((st >> 3) & 0x03) != 3;
    return volts > 2.4f;
}

bool batteryInit() {
    gauge.init();  // same provisioning as LilyGO factory firmware; result not required
    uint16_t mv = 0;
    bool ok = gaugeWord(0x08, mv) && mv > 2500 && mv < 4600;
    Serial.printf("[batt] BQ27220 %s (%u mV)\n", ok ? "OK" : TR("brak odpowiedzi", "no response"), mv);
    float v;
    bool ch;
    bool chg = chargerBattery(v, ch);
    Serial.printf("[batt] BQ25896 ADC %s (%.2f V)\n", chg ? "OK" : TR("brak", "none"), v);
    return ok || chg;
}

Battery batteryRead() {
    Battery b;
    uint16_t mv, soc, rem;
    if (gaugeWord(0x08, mv) && mv > 2500 && mv < 4600 && gaugeWord(0x2C, soc) && soc <= 100) {
        b.ok = true;
        b.source = "BQ27220";
        b.volts = mv / 1000.0f;
        b.pct = soc;
        uint16_t cur;
        if (gaugeWord(0x0C, cur)) b.ma = (int16_t)cur;
        if (gaugeWord(0x10, rem)) {
            b.remMah = rem;
            if (b.ma < -5) b.minutesLeft = (int)(rem * 60L / (-b.ma));
        }
        uint16_t w;
        if (gaugeWord(0x12, w) && w > 0 && w < 20000) b.fullMah = w;
        if (b.ma > 5 && gaugeWord(0x18, w) && w < 65535) b.minutesToFull = w;  // TimeToFull, 65535 = n/a
        b.charging = b.ma > 5;
        uint8_t st;
        if (i2cRead(BQ25896_ADDR, 0x0B, &st, 1)) b.chgStat = (st >> 3) & 0x03;
        return b;
    }
    float v;
    bool ch;
    if (chargerBattery(v, ch)) {
        b.ok = true;
        b.source = TR("BQ25896 (napięcie)", "BQ25896 (voltage)");
        b.volts = v;
        b.pct = lipoPercent(v);
        b.charging = ch;
        return b;
    }
    // nothing worked: report which chip does not acknowledge on the bus
    static char why[48];
    Wire.beginTransmission(BQ27220_ADDR);
    bool g = Wire.endTransmission() == 0;
    Wire.beginTransmission(BQ25896_ADDR);
    bool c = Wire.endTransmission() == 0;
    snprintf(why, sizeof why, TR("brak (55:%s 6B:%s)", "none (55:%s 6B:%s)"), g ? TR("jest", "yes") : TR("nie", "no"), c ? TR("jest", "yes") : TR("nie", "no"));
    b.source = why;
    return b;
}

std::string i2cScan() {
    std::string out;
    char b[4];
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            snprintf(b, sizeof b, "%02X ", a);
            out += b;
        }
    }
    return out.empty() ? TR("nic", "nothing") : out;
}

// ------------------------------------------------------------------ front light
void backlight(int pct) {
    static bool init = false;
    if (!init) {
        ledcSetup(0, 1000, 8);  // channel 0, 1 kHz (LilyGO: keep PWM <= ~1 kHz), 8 bit
        ledcAttachPin(PIN_BL_EN, 0);
        init = true;
    }
    pct = constrain(pct, 0, 100);
    ledcWrite(0, pct * 255 / 100);
}

// ------------------------------------------------------------------ power
static bool chgRead(uint8_t reg, uint8_t& v) { return i2cRead(BQ25896_ADDR, reg, &v, 1); }
static bool chgWrite(uint8_t reg, uint8_t v) { return i2cWrite8(BQ25896_ADDR, reg, v); }

bool usbPowered() {
    uint8_t s;
    if (!chgRead(0x0B, s)) return false;
    return (s & 0x04) != 0;  // REG0B bit2 PG_STAT: input power good
}

bool powerOff() {
    if (usbPowered()) return false;
    uint8_t r;
    if (!chgRead(0x09, r)) return false;
    r &= ~0x08;  // BATFET_DLY = 0: turn off immediately
    r |= 0x20;   // BATFET_DIS = 1 (same as XPowersLib PPM.shutdown() used by LilyGO factory firmware)
    chgWrite(0x09, r);
    delay(2000);
    return false;  // still running -> it did not work
}

// ------------------------------------------------------------------ SD
bool sdInit() {
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    // mount point "/sd" -> plain fopen("/sd/...") works everywhere (navcore uses stdio)
    return SD.begin(PIN_SD_CS, SPI, 20000000, SD_MOUNT, 8);
}

float sdFreeMb() {
    uint64_t total = SD.totalBytes(), used = SD.usedBytes();
    return total > used ? (float)((total - used) / (1024.0 * 1024.0)) : 0.f;
}

}  // namespace board
