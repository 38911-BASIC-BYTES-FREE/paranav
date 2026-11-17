// File upload protocol (phone -> ParaNav over BLE). Transport independent, host testable.
//
// Control channel (text lines, phone -> device):
//   HELLO                                         -> "PARANAV 1 <fw>"
//   BEGIN <kind> <size> <crc32hex> <replace> <name>   kind: airspace | activations | waypoints | route
//                                                 -> "OK BEGIN <window>" | "ERR <reason>"
//   END                                           -> "OK RECEIVED" (file verified, installation queued),
//                                                    later "OK END <count>" or "ERR <reason>" from install
//   ABORT                                         -> "OK ABORT"
//   LIST                                          -> "FILE <path> <size>" ... "OK LIST <n>"
//   DEL <path>                                    -> "OK DEL" | "ERR ..."
// Data channel (binary): [uint32 offset, little endian][payload]
//   device answers "ACK <received>" every <window> bytes and at the end,
//   "ERR SEQ <received>" when a chunk does not start where expected (phone resends from there).
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nav {

uint32_t crc32Update(uint32_t crc, const uint8_t* d, size_t n);  // standard CRC-32 (zlib), start with 0

class FileReceiver {
public:
    using Notify = std::function<void(const std::string&)>;
    static constexpr uint32_t kWindow = 4096;
    static constexpr uint32_t kMaxSize = 8u * 1024 * 1024;

    explicit FileReceiver(const std::string& root) : root_(root) {}
    void setNotify(Notify n) { notify_ = std::move(n); }
    void setFirmware(const std::string& fw) { fw_ = fw; }

    // Called from the transport (BLE host task)
    void onCtrl(const std::string& line);
    void onData(const uint8_t* d, size_t n);
    void onDisconnect();  // abort an unfinished transfer

    // A verified upload waiting to be installed by the main task
    struct Pending {
        std::string kind, name, tmpPath;
        bool replace = false;
    };
    bool takePending(Pending& p);
    // Install (move into place). Returns false + message on error; count = zones / waypoints installed.
    static bool install(const std::string& root, const Pending& p, std::string& msg, int& count);

    // Progress for the UI
    bool active() const { return file_ != nullptr; }
    // thread safe copies (the strings are written by the BLE task, read by the UI)
    std::string name() const {
        std::lock_guard<std::mutex> l(strMtx_);
        return name_;
    }
    std::string kind() const {
        std::lock_guard<std::mutex> l(strMtx_);
        return kind_;
    }
    float progress() const { return size_ ? (float)received_ / size_ : 0.f; }
    std::string lastResult() const;

    static std::string sanitize(const std::string& name, const char* ext);
    static std::string targetPath(const std::string& root, const std::string& kind, const std::string& name);

private:
    void send(const std::string& s) {
        if (notify_) notify_(s);
    }
    void closeTemp(bool remove);
    std::string tmpPath() const { return root_ + "/.paranav_upload.tmp"; }

    std::string root_, fw_ = "?";
    Notify notify_;
    FILE* file_ = nullptr;
    std::string kind_, name_;
    bool replace_ = false;
    uint32_t size_ = 0, crcExpected_ = 0, crc_ = 0, received_ = 0, lastAck_ = 0;
    bool seqErrSent_ = false;
    std::mutex mtx_;
    bool pendingReady_ = false;
    Pending pending_;
    std::string lastResult_;
    mutable std::mutex strMtx_;
    void setStrings(const std::string& kind, const std::string& name) {
        std::lock_guard<std::mutex> l(strMtx_);
        kind_ = kind;
        name_ = name;
    }
    void setResult(const std::string& r) {
        std::lock_guard<std::mutex> l(strMtx_);
        lastResult_ = r;
    }
};

}  // namespace nav
