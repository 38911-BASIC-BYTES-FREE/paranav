#include "fanet_radio.h"
#include "i18n.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_system.h>

#include <mutex>

#include "config.h"

namespace fanet {

// The SX1262 shares the SPI bus with the SD card. Both drivers wrap every access in
// SPI.beginTransaction()/endTransaction(), which holds the bus mutex of the Arduino SPI class,
// so card access and radio commands never interleave.
static SX1262 radio = new Module(PIN_LORA_CS, PIN_LORA_IRQ, PIN_LORA_RST, PIN_LORA_BUSY, SPI);

static std::mutex mtx;
static Config cfg;
static OwnTx own;
static Status st;
static QueueHandle_t rxQ = nullptr;
static TaskHandle_t taskH = nullptr;

// crash guard: if the device dies while the radio starts, keep FANET off on the next boot
RTC_NOINIT_ATTR static uint32_t gLoraGuard;
static constexpr uint32_t kGuard = 0x4C4F5241;  // "LORA"
static bool guardTripped = false;

static void IRAM_ATTR onDio1() {
    BaseType_t woken = pdFALSE;
    if (taskH) vTaskNotifyGiveFromISR(taskH, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void setErr(const std::string& e) {
    std::lock_guard<std::mutex> l(mtx);
    st.state = 3;
    st.err = e;
    Serial.printf("[fanet] %s\n", e.c_str());
}

static bool radioInit() {
    gLoraGuard = kGuard;
    // EU FANET: 868.2 MHz, BW 250 kHz, SF7, CR 4/5, sync word 0xF1, 14 dBm, preamble 8,
    // TCXO 2.4 V and DIO2 as RF switch like the LilyGO examples for this board
    int16_t e = radio.begin(nav::kFanetFreqMHz, 250.0, 7, 5, nav::kFanetSyncWord, 14, 8, 2.4f, false);
    if (e == RADIOLIB_ERR_NONE) e = radio.setDio2AsRfSwitch(true);
    if (e == RADIOLIB_ERR_NONE) e = radio.setCurrentLimit(140);
    if (e == RADIOLIB_ERR_NONE) e = radio.setCRC(true);
    gLoraGuard = 0;
    if (e != RADIOLIB_ERR_NONE) {
        char b[48];
        if (e == RADIOLIB_ERR_CHIP_NOT_FOUND) snprintf(b, sizeof b, TR("brak modułu LoRa", "no LoRa module"));
        else snprintf(b, sizeof b, TR("błąd radia LoRa (%d)", "LoRa radio error (%d)"), (int)e);
        setErr(b);
        return false;
    }
    radio.setDio1Action(onDio1);
    return true;
}

static constexpr uint16_t kRxFlags =
    RADIOLIB_SX126X_IRQ_RX_DEFAULT | RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID;

static void startRx() { radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, kRxFlags, RADIOLIB_SX126X_IRQ_RX_DONE); }

static void readPacket() {
    Rx r;
    size_t len = radio.getPacketLength();
    int16_t e = RADIOLIB_ERR_UNKNOWN;
    if (len > 0 && len <= sizeof r.data) e = radio.readData(r.data, len);
    else radio.readData(r.data, 0);  // clears the IRQ flags
    std::lock_guard<std::mutex> l(mtx);
    if (e == RADIOLIB_ERR_NONE) {
        r.len = (uint8_t)len;
        r.rssi = (int16_t)radio.getRSSI();
        r.ms = millis();
        st.rx++;
        st.rssi = r.rssi;
        if (rxQ) xQueueSend(rxQ, &r, 0);
    } else {
        st.rxErr++;
    }
}

// Transmit with a simple "listen before talk": if a LoRa header is being received right now,
// wait for that packet to end (FANET packets last ~25 ms).
static bool transmit(uint8_t* buf, size_t len) {
    for (int i = 0; i < 4; i++) {
        uint16_t irq = radio.getIrqStatus();
        if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) readPacket();
        else if (!(irq & RADIOLIB_SX126X_IRQ_HEADER_VALID)) break;
        vTaskDelay(pdMS_TO_TICKS(30 + (esp_random() % 40)));
    }
    ulTaskNotifyTake(pdTRUE, 0);
    if (radio.startTransmit(buf, len) != RADIOLIB_ERR_NONE) {
        startRx();
        return false;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300));  // TX_DONE on DIO1
    radio.finishTransmit();
    startRx();
    return true;
}

static void task(void*) {
    bool up = false;
    uint32_t nextTx = millis() + 3000, nextName = millis() + 8000;
    for (;;) {
        Config c;
        OwnTx o;
        {
            std::lock_guard<std::mutex> l(mtx);
            c = cfg;
            o = own;
        }
        if (!c.rx || guardTripped) {
            if (up) {
                radio.sleep();
                up = false;
            }
            {
                std::lock_guard<std::mutex> l(mtx);
                if (!guardTripped) {  // switching off clears an init error -> retried when switched on
                    st.state = 0;
                    st.err.clear();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!up) {
            {
                std::lock_guard<std::mutex> l(mtx);
                if (st.state == 3) {  // init failed before: do not hammer the chip
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
            }
            if (!radioInit()) continue;
            startRx();
            up = true;
        }
        {
            std::lock_guard<std::mutex> l(mtx);
            st.state = c.tx ? 2 : 1;
        }
        // wait for a packet (DIO1) or the next transmit slot
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50))) {
            if (radio.getIrqStatus() & RADIOLIB_SX126X_IRQ_RX_DONE) readPacket();
        }
        uint32_t now = millis();
        if (c.tx && o.valid && c.addr && (int32_t)(now - nextTx) >= 0) {
            uint8_t b[nav::kFanetMaxPacket];
            size_t n = o.flying ? nav::fanetEncodeTracking(b, sizeof b, c.addr, o.pos)
                                : nav::fanetEncodeGround(b, sizeof b, c.addr, o.pos.lat, o.pos.lon, 0, o.pos.online);
            if (n && transmit(b, n)) {
                std::lock_guard<std::mutex> l(mtx);
                st.tx++;
            }
            nextTx = now + nav::fanetTrackingIntervalMs(o.neighbours) + (esp_random() % 500);
        }
        if (c.tx && c.addr && !c.name.empty() && (int32_t)(now - nextName) >= 0) {
            uint8_t b[nav::kFanetMaxPacket];
            size_t n = nav::fanetEncodeName(b, sizeof b, c.addr, c.name);
            if (n) transmit(b, n);
            nextName = now + 240000 + (esp_random() % 2000);  // every 4 min
        }
    }
}

void begin() {
    esp_reset_reason_t rr = esp_reset_reason();
    bool crashed = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT;
    if (crashed && gLoraGuard == kGuard) {
        guardTripped = true;
        st.state = 3;
        st.err = TR("błąd startu radia – FANET wyłączony", "radio start failed – FANET disabled");
    }
    gLoraGuard = 0;
    rxQ = xQueueCreate(16, sizeof(Rx));
    xTaskCreatePinnedToCore(task, "fanet", 6144, nullptr, 4, &taskH, 1);
}

void configure(const Config& c) {
    std::lock_guard<std::mutex> l(mtx);
    cfg = c;
}

void setOwn(const OwnTx& o) {
    std::lock_guard<std::mutex> l(mtx);
    own = o;
}

bool getRx(Rx& r) { return rxQ && xQueueReceive(rxQ, &r, 0) == pdTRUE; }

Status status() {
    std::lock_guard<std::mutex> l(mtx);
    return st;
}

}  // namespace fanet
