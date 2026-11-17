#include "phone_ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <mutex>

#include "config.h"

namespace phone {

static nav::FileReceiver rx(SD_MOUNT);
static NimBLEServer* server = nullptr;
static NimBLECharacteristic* ctrl = nullptr;
static volatile bool on = true;
static volatile bool connected = false;
static bool advertising = false;
static std::mutex mtx;
static std::string lastResult;
static std::string ctrlBuf;  // control lines may arrive split over several writes

void notify(const std::string& s) {
    if (!ctrl || !connected) return;
    ctrl->setValue(s + "\n");
    ctrl->notify();
}

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
        connected = true;
        ctrlBuf.clear();
        Serial.println("[phone] connected");
    }
    void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
        connected = false;
        rx.onDisconnect();
        Serial.println("[phone] disconnected");
    }
};

class CtrlCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        NimBLEAttValue v = c->getValue();
        ctrlBuf.append((const char*)v.data(), v.length());
        size_t nl;
        while ((nl = ctrlBuf.find('\n')) != std::string::npos) {
            std::string line = ctrlBuf.substr(0, nl);
            ctrlBuf.erase(0, nl + 1);
            rx.onCtrl(line);
        }
        if (ctrlBuf.size() > 512) ctrlBuf.clear();
    }
};

class DataCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        NimBLEAttValue v = c->getValue();
        rx.onData(v.data(), v.length());
    }
};

void setupServer() {
    if (server) return;
    rx.setFirmware(PARANAV_VERSION);
    rx.setNotify(notify);
    NimBLEDevice::setMTU(247);
    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCB());
    NimBLEService* svc = server->createService(kSvcUuid);
    ctrl = svc->createCharacteristic(kCtrlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    ctrl->setCallbacks(new CtrlCB());
    NimBLECharacteristic* data = svc->createCharacteristic(kDataUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE);
    data->setCallbacks(new DataCB());
    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(kSvcUuid);
    adv->setScanResponse(true);
    adv->setName("ParaNav");
    Serial.println("[phone] BLE service ready");
}

void tick() {
    if (!server) return;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    bool want = on && !connected;
    if (want && !adv->isAdvertising()) adv->start();
    if (!want && adv->isAdvertising()) adv->stop();
    if (!on && connected) {
        // switched off in the menu: drop the phone
        for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
    }
    advertising = adv->isAdvertising();
}

void setEnabled(bool v) { on = v; }
bool enabled() { return on; }
bool busy() { return rx.active(); }

Status status() {
    Status s;
    s.enabled = on;
    s.connected = connected;
    s.receiving = rx.active();
    s.kind = rx.kind();
    s.name = rx.name();
    s.progress = rx.progress();
    std::lock_guard<std::mutex> l(mtx);
    s.last = lastResult.empty() ? rx.lastResult() : lastResult;
    return s;
}

bool takePending(nav::FileReceiver::Pending& p) { return rx.takePending(p); }

void setLastResult(const std::string& s) {
    std::lock_guard<std::mutex> l(mtx);
    lastResult = s;
}

}  // namespace phone
