// FANET (Flying Ad-hoc Network, open LoRa protocol used by paragliders): packet coding and the
// table of nearby aircraft with a simple conflict estimate.
// Spec: https://github.com/3s1d/fanet-stm32/blob/master/Src/fanet/radio/protocol.txt
// Radio (EU): 868.2 MHz, LoRa SF7, BW 250 kHz, CR 4/5..4/8, explicit header, CRC, sync word 0xF1.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nav {

constexpr float kFanetFreqMHz = 868.2f;
constexpr uint8_t kFanetSyncWord = 0xF1;
constexpr uint8_t kFanetManufacturer = 0xFD;  // "unregistered device" range of the spec
constexpr size_t kFanetMaxPacket = 64;

enum FanetType : uint8_t { FT_ACK = 0, FT_TRACKING = 1, FT_NAME = 2, FT_MESSAGE = 3, FT_SERVICE = 4, FT_GROUND = 7,
                           FT_THERMAL = 9 };
enum FanetAircraft : uint8_t { FA_OTHER = 0, FA_PARAGLIDER, FA_HANGGLIDER, FA_BALLOON, FA_GLIDER, FA_POWERED,
                               FA_HELICOPTER, FA_UAV };

struct FanetOwn {
    double lat = 0, lon = 0;
    float altM = 0;        // GPS altitude AMSL
    float speedKmh = 0, climbMs = 0, headingDeg = 0;
    uint8_t aircraft = FA_PARAGLIDER;
    bool online = true;    // "online tracking" bit: may be shown on the internet (OGN, live tracking)
};

// Encoders: return packet length (0 if it does not fit). addr = manufacturer << 16 | id.
size_t fanetEncodeTracking(uint8_t* buf, size_t cap, uint32_t addr, const FanetOwn& o);
size_t fanetEncodeGround(uint8_t* buf, size_t cap, uint32_t addr, double lat, double lon, uint8_t groundType,
                         bool online);
size_t fanetEncodeName(uint8_t* buf, size_t cap, uint32_t addr, const std::string& name);

struct FanetPacket {
    uint8_t type = 0;
    uint32_t src = 0;
    bool forward = false;
    // tracking / ground tracking
    bool hasPos = false;
    double lat = 0, lon = 0;
    float altM = 0, speedKmh = 0, climbMs = 0, headingDeg = 0;
    uint8_t aircraft = 0;
    bool online = false;
    uint8_t groundType = 0;
    std::string name;  // FT_NAME
    // service (weather station)
    bool hasWind = false;
    float windDirDeg = 0, windKmh = 0, gustKmh = 0;
};
bool fanetDecode(const uint8_t* buf, size_t len, FanetPacket& out);
std::string fanetAddrStr(uint32_t addr);  // "FD1A2B"

// FANET rule: tracking every floor((neighbours/10 + 1) * 5 s)
uint32_t fanetTrackingIntervalMs(int neighbours);

struct Traffic {
    uint32_t addr = 0;
    std::string name;
    uint8_t aircraft = 0;
    bool ground = false;     // ground tracking (pilot on the ground / vehicle)
    uint8_t groundType = 0;
    double lat = 0, lon = 0;
    float altM = 0, speedKmh = 0, climbMs = 0, headingDeg = 0;
    uint32_t lastMs = 0;
    int rssi = 0;
    // filled by TrafficDb::evaluate()
    float ageS = 0;
    float distM = 0, brgDeg = 0, dzM = 0;   // now (position extrapolated by its speed)
    float cpaM = 0, cpaS = 0, dzCpaM = 0;    // closest point of approach within 60 s
    int level = 0;                           // 0 info, 1 nearby, 2 caution, 3 alarm
    bool stale() const { return ageS > 20; }
};

struct TrafficOwn {
    bool valid = false;
    bool flying = false;
    double lat = 0, lon = 0;
    float altM = 0, speedKmh = 0, trackDeg = 0, climbMs = 0;
    float warnM = 300, vertM = 100;  // caution thresholds; alarm = half of them
};

class TrafficDb {
public:
    void onPacket(const FanetPacket& p, uint32_t nowMs, int rssi);
    // Ages, extrapolates and rates every entry; drops entries older than 120 s; sorts by
    // level (highest first), then distance.
    void evaluate(const TrafficOwn& own, uint32_t nowMs);
    const std::vector<Traffic>& list() const { return list_; }
    const Traffic* find(uint32_t addr) const;
    int recent(uint32_t nowMs, uint32_t withinMs = 60000) const;  // aircraft heard recently
    int maxLevel() const;
    void clear() { list_.clear(); }

private:
    Traffic& entry(uint32_t addr, uint32_t nowMs);
    std::vector<Traffic> list_;
};

// "na godz. 2": clock position of a bearing relative to the track (12 = straight ahead)
int clockPosition(float relDeg);

}  // namespace nav
