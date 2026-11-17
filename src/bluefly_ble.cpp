#include "bluefly_ble.h"
#include "phone_ble.h"

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_attr.h>

namespace bluefly {

// Microchip Transparent UART service (RNBD350 / RN487x / BM7x)
static const char* kSvc = "49535343-FE7D-4AE5-8FA9-9FAFD205E455";
static const char* kTx = "49535343-1E4D-4BD9-BA61-23C647249616";  // module -> us (notify)

static QueueHandle_t q = nullptr;
static SemaphoreHandle_t mtx = nullptr;
static nav::BlueFlyParser parser;
static Status st;
static volatile bool enabled = false;
static volatile bool dropRequested = false;
static NimBLEClient* client = nullptr;
static bool nimbleInit = false;

// Crash guard: set while the Bluetooth stack is starting. If the device resets with a panic
// during that time, Bluetooth stays off on the next boot so the device remains usable.
RTC_NOINIT_ATTR static uint32_t gBtInitGuard;
static constexpr uint32_t kGuard = 0x42544E49;  // "BTNI"
static constexpr uint32_t kStartDelayMs = 5000;  // start Bluetooth after the rest of the system is up
static constexpr size_t kMinFreeInternal = 70 * 1024;
static constexpr size_t kMinLargestBlock = 32 * 1024;

static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    // runs in the NimBLE host task: parse the text stream and queue the measurements
    lock();
    for (size_t i = 0; i < len; i++) {
        if (parser.feed((char)data[i])) {
            const auto& smp = parser.sample();
            Sample s{smp.kind, smp.kind == nav::BlueFlyParser::Kind::Pressure ? smp.pressureHpa : smp.altStd,
                     (uint32_t)micros()};
            xQueueSend(q, &s, 0);  // drop when full
            st.lastDataMs = millis();
        }
    }
    st.batteryPct = parser.batteryPct();
    st.batteryV = parser.batteryVolts();
    st.tempC = parser.temperatureC();
    st.format = parser.format();
    st.lines = parser.lines();
    unlock();
}

static void setState(State s) {
    lock();
    st.state = s;
    unlock();
}

static bool connectTo(NimBLEAdvertisedDevice* dev) {
    setState(State::Connecting);
    if (!client) {
        client = NimBLEDevice::createClient();
        client->setConnectTimeout(6);
    }
    if (!client->connect(dev)) return false;
    NimBLERemoteService* svc = client->getService(kSvc);
    NimBLERemoteCharacteristic* ch = svc ? svc->getCharacteristic(kTx) : nullptr;
    if (!ch || !ch->canNotify() || !ch->subscribe(true, onNotify)) {
        client->disconnect();
        return false;
    }
    lock();
    st.name = dev->getName();
    if (st.name.empty()) st.name = dev->getAddress().toString();
    st.state = State::Connected;
    st.lastDataMs = millis();
    unlock();
    Serial.printf("[bluefly] connected to %s\n", st.name.c_str());
    return true;
}

static void setError(const std::string& e) {
    lock();
    st.state = State::Error;
    st.error = e;
    unlock();
    Serial.printf("[bluefly] %s\n", e.c_str());
}

static bool startStack() {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("[bluefly] internal RAM free %u B, largest block %u B\n", (unsigned)freeInt, (unsigned)largest);
    if (freeInt < kMinFreeInternal || largest < kMinLargestBlock) {
        char b[64];
        snprintf(b, sizeof b, "za mało RAM (%u kB)", (unsigned)(freeInt / 1024));
        setError(b);
        return false;
    }
    gBtInitGuard = kGuard;
    NimBLEDevice::init("ParaNav");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    gBtInitGuard = 0;
    nimbleInit = true;
    Serial.printf("[bluefly] Bluetooth started, internal RAM free %u B\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
}

static void task(void*) {
    uint32_t lastRssi = 0;
    vTaskDelay(pdMS_TO_TICKS(kStartDelayMs));
    for (;;) {
        bool wantPhone = phone::enabled();
        if (!nimbleInit) {
            if (!enabled && !wantPhone) {  // nobody needs Bluetooth: do not even start it
                lock();
                st.state = State::Off;
                st.error.clear();
                unlock();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            lock();
            bool err = st.state == State::Error;
            unlock();
            if (err || !startStack()) {
                vTaskDelay(pdMS_TO_TICKS(1000));  // stays in Error until switched off/on in the menu
                continue;
            }
            phone::setupServer();
        }
        phone::tick();
        if (!enabled) {
            if (client && client->isConnected()) client->disconnect();
            lock();
            if (st.state != State::Error) st.state = State::Off;
            unlock();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (client && client->isConnected()) {
            if (dropRequested) {
                client->disconnect();
                dropRequested = false;
                continue;
            }
            if (millis() - lastRssi > 5000) {
                int r = client->getRssi();
                lock();
                st.rssi = r;
                unlock();
                lastRssi = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        dropRequested = false;
        if (phone::busy()) {  // a file is coming from the phone: do not disturb the radio with scanning
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        // not connected: scan for 4 s
        setState(State::Scanning);
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(60);
        NimBLEScanResults res = scan->start(4, false);
        NimBLEAdvertisedDevice* found = nullptr;
        for (auto it = res.begin(); it != res.end(); ++it) {
            NimBLEAdvertisedDevice* d = *it;
            std::string n = d->getName();
            if (n.rfind("BlueFly", 0) == 0 || n.rfind("BLUEFLY", 0) == 0 || d->isAdvertisingService(NimBLEUUID(kSvc))) {
                if (!found || d->getRSSI() > found->getRSSI()) found = d;
            }
        }
        bool ok = found && connectTo(found);
        scan->clearResults();
        if (!ok) {
            setState(State::Scanning);
            for (int i = 0; i < (found ? 4 : 12); i++) {  // pause between scans saves battery
                phone::tick();
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
}

void begin(bool on) {
    q = xQueueCreate(64, sizeof(Sample));
    mtx = xSemaphoreCreateMutex();
    enabled = on;
    esp_reset_reason_t rr = esp_reset_reason();
    bool crashed = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT;
    if (on && crashed && gBtInitGuard == kGuard) {
        // the previous run died while starting Bluetooth: keep it off this time
        st.state = State::Error;
        st.error = "błąd startu Bluetooth – wyłączony";
        Serial.println("[bluefly] previous crash during Bluetooth start - Bluetooth disabled for this run");
    }
    gBtInitGuard = 0;
    xTaskCreatePinnedToCore(task, "bluefly", 12288, nullptr, 2, nullptr, 0);
}

void setEnabled(bool on) {
    enabled = on;
    if (on && mtx) {  // switching on in the menu clears a previous error and tries again
        lock();
        if (st.state == State::Error) st.state = State::Off;
        st.error.clear();
        unlock();
    }
}

void reconnect() { dropRequested = true; }

void retryStack() {
    if (!mtx) return;
    lock();
    if (st.state == State::Error) st.state = State::Off;
    st.error.clear();
    unlock();
}

bool getSample(Sample& s) { return q && xQueueReceive(q, &s, 0) == pdTRUE; }

Status status() {
    lock();
    Status s = st;
    unlock();
    return s;
}

}  // namespace bluefly
