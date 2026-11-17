#include "filexfer.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "airspace.h"
#include "waypoints.h"
#include "yield.h"

namespace nav {

uint32_t crc32Update(uint32_t crc, const uint8_t* d, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static bool endsWithCI(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suf) == 0;
}

std::string FileReceiver::sanitize(const std::string& name, const char* ext) {
    // keep the base name only, ASCII letters/digits/._- ; force the extension
    std::string base = name;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    // transliterate Polish letters (UTF-8) so "Łódź strefy.txt" becomes "Lodz_strefy.txt"
    static const char* pl[][2] = {{"ą", "a"}, {"ć", "c"}, {"ę", "e"}, {"ł", "l"}, {"ń", "n"}, {"ó", "o"},
                                  {"ś", "s"}, {"ź", "z"}, {"ż", "z"}, {"Ą", "A"}, {"Ć", "C"}, {"Ę", "E"},
                                  {"Ł", "L"}, {"Ń", "N"}, {"Ó", "O"}, {"Ś", "S"}, {"Ź", "Z"}, {"Ż", "Z"}};
    for (auto& m : pl) {
        size_t pos;
        while ((pos = base.find(m[0])) != std::string::npos) base.replace(pos, std::strlen(m[0]), m[1]);
    }
    std::string out;
    for (unsigned char c : base) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') out += (char)c;
        else if (c == ' ') out += '_';
    }
    while (!out.empty() && out[0] == '.') out.erase(0, 1);  // no hidden files
    if (endsWithCI(out, ext)) out.erase(out.size() - std::strlen(ext));
    if (out.size() > 40) out.resize(40);
    if (out.empty()) out = "plik";
    return out + ext;
}

std::string FileReceiver::targetPath(const std::string& root, const std::string& kind, const std::string& name) {
    if (kind == "airspace") return root + "/airspace/" + sanitize(name, ".txt");
    if (kind == "activations") return root + "/airspace/activations.txt";
    if (kind == "waypoints") return root + "/waypoints/" + sanitize(name, ".cup");
    if (kind == "route") return root + "/route/" + sanitize(name, ".cup");
    return "";
}

void FileReceiver::closeTemp(bool remove) {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    if (remove) std::remove(tmpPath().c_str());
}

std::string FileReceiver::lastResult() const {
    std::lock_guard<std::mutex> l(strMtx_);
    return lastResult_;
}

void FileReceiver::onDisconnect() {
    if (file_) {
        closeTemp(true);
        setResult("przerwano (rozłączono)");
    }
}

void FileReceiver::onCtrl(const std::string& lineIn) {
    std::string line = lineIn;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line == "HELLO") {
        send("PARANAV 1 " + fw_);
        return;
    }
    if (line.rfind("BEGIN ", 0) == 0) {
        closeTemp(true);
        char kind[16] = {0};
        unsigned long size = 0, crc = 0;
        int replace = 0, used = 0;
        if (std::sscanf(line.c_str() + 6, "%15s %lu %lx %d %n", kind, &size, &crc, &replace, &used) < 4 || used == 0) {
            send("ERR BEGIN");
            return;
        }
        std::string k = kind;
        std::string nm = line.substr(6 + used);
        if (targetPath(root_, k, nm.empty() ? "plik" : nm).empty()) {
            send("ERR KIND");
            return;
        }
        if (size == 0 || size > kMaxSize) {
            send("ERR SIZE");
            return;
        }
        file_ = std::fopen(tmpPath().c_str(), "wb");
        if (!file_) {
            send("ERR SD");
            return;
        }
        setStrings(k, nm.empty() ? "plik" : nm);
        replace_ = replace != 0;
        size_ = (uint32_t)size;
        crcExpected_ = (uint32_t)crc;
        crc_ = 0;
        received_ = lastAck_ = 0;
        seqErrSent_ = false;
        setResult("");
        send("OK BEGIN " + std::to_string(kWindow));
        return;
    }
    if (line == "END") {
        if (!file_) {
            send("ERR NOFILE");
            return;
        }
        std::fflush(file_);
        bool ok = received_ == size_ && crc_ == crcExpected_;
        closeTemp(!ok);
        if (!ok) {
            setResult("błąd sumy kontrolnej");
            send(received_ != size_ ? "ERR SIZE" : "ERR CRC");
            return;
        }
        {
            std::lock_guard<std::mutex> l(mtx_);
            // move the verified temp file aside so a new upload cannot overwrite it before installation
            std::string queued = root_ + "/.paranav_queued.tmp";
            std::remove(queued.c_str());
            std::rename(tmpPath().c_str(), queued.c_str());
            pending_ = Pending{kind_, name_, queued, replace_};
            pendingReady_ = true;
        }
        send("OK RECEIVED");
        return;
    }
    if (line == "ABORT") {
        closeTemp(true);
        send("OK ABORT");
        return;
    }
    if (line == "LIST") {
        int n = 0;
        static const char* dirs[] = {"airspace", "waypoints", "route"};
        for (const char* d : dirs) {
            std::string p = root_ + "/" + d;
            DIR* dir = opendir(p.c_str());
            if (!dir) continue;
            while (dirent* e = readdir(dir)) {
                if (e->d_name[0] == '.') continue;
                struct stat st;
                std::string full = p + "/" + e->d_name;
                if (stat(full.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
                send(std::string("FILE ") + d + "/" + e->d_name + " " + std::to_string((long)st.st_size));
                n++;
            }
            closedir(dir);
        }
        send("OK LIST " + std::to_string(n));
        return;
    }
    if (line.rfind("DEL ", 0) == 0) {
        std::string p = line.substr(4);
        size_t slash = p.find('/');
        std::string dir = slash == std::string::npos ? "" : p.substr(0, slash);
        std::string file = slash == std::string::npos ? "" : p.substr(slash + 1);
        bool allowed = (dir == "airspace" || dir == "waypoints" || dir == "route") && !file.empty() &&
                       file.find('/') == std::string::npos && file.find("..") == std::string::npos && file[0] != '.';
        if (!allowed) {
            send("ERR PATH");
            return;
        }
        if (std::remove((root_ + "/" + p).c_str()) == 0) {
            std::lock_guard<std::mutex> l(mtx_);
            pending_ = Pending{"reload", file, "", false};  // ask the main task to reload data
            pendingReady_ = true;
            send("OK DEL");
        } else {
            send("ERR DEL");
        }
        return;
    }
    send("ERR CMD");
}

void FileReceiver::onData(const uint8_t* d, size_t n) {
    if (!file_ || n < 4) return;
    uint32_t off = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    const uint8_t* p = d + 4;
    size_t len = n - 4;
    if (off != received_) {
        if (!seqErrSent_) {
            send("ERR SEQ " + std::to_string(received_));
            seqErrSent_ = true;
        }
        return;
    }
    seqErrSent_ = false;
    if (received_ + len > size_) len = size_ - received_;
    if (len && std::fwrite(p, 1, len, file_) != len) {
        closeTemp(true);
        send("ERR SD");
        return;
    }
    crc_ = crc32Update(crc_, p, len);
    received_ += (uint32_t)len;
    if (received_ - lastAck_ >= kWindow || received_ == size_) {
        lastAck_ = received_;
        send("ACK " + std::to_string(received_));
    }
}

bool FileReceiver::takePending(Pending& p) {
    std::lock_guard<std::mutex> l(mtx_);
    if (!pendingReady_) return false;
    p = pending_;
    pendingReady_ = false;
    return true;
}

static void removeOthers(const std::string& dir, const char* ext, const std::string& keep) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> del;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        std::string full = dir + "/" + n;
        if (n[0] == '.' || !endsWithCI(n, ext) || full == keep || strcasecmp(n.c_str(), "activations.txt") == 0)
            continue;
        del.push_back(full);
    }
    closedir(d);
    for (auto& f : del) std::remove(f.c_str());
}

// Fast plausibility check of an OpenAir file (the full parse happens once, when the data is reloaded):
// number of "AC" lines that are followed by geometry (DP/DC/DA/DB).
static int quickCountOpenAir(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int zones = 0;
    bool open = false, geom = false;
    while (std::fgets(line, sizeof line, f)) {
        maybeYield();
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        char a = (char)std::toupper((unsigned char)p[0]), b = (char)std::toupper((unsigned char)p[1]);
        if (a == 'A' && b == 'C') {
            if (open && geom) zones++;
            open = true;
            geom = false;
        } else if (open && a == 'D' && (b == 'P' || b == 'C' || b == 'A' || b == 'B')) {
            geom = true;
        }
    }
    if (open && geom) zones++;
    std::fclose(f);
    return zones;
}

bool FileReceiver::install(const std::string& root, const Pending& p, std::string& msg, int& count) {
    count = 0;
    std::string target = targetPath(root, p.kind, p.name);
    if (target.empty()) {
        msg = "nieznany typ pliku";
        return false;
    }
    // validate the content before it replaces anything
    if (p.kind == "airspace") {
        count = quickCountOpenAir(p.tmpPath.c_str());
        if (count <= 0) {
            std::remove(p.tmpPath.c_str());
            msg = "brak stref w pliku (czy to format OpenAir?)";
            return false;
        }
    } else if (p.kind == "waypoints") {
        WaypointDb db;
        count = db.loadCup(p.tmpPath.c_str());
        if (count <= 0) {
            std::remove(p.tmpPath.c_str());
            msg = "brak punktów w pliku (czy to SeeYou .cup?)";
            return false;
        }
    } else if (p.kind == "route") {
        Route r;
        count = r.loadCup(p.tmpPath.c_str());
        if (count <= 0) {
            std::remove(p.tmpPath.c_str());
            msg = "brak punktów trasy w pliku";
            return false;
        }
    } else if (p.kind == "activations") {
        FILE* f = std::fopen(p.tmpPath.c_str(), "r");
        char line[160];
        while (f && std::fgets(line, sizeof line, f))
            if (line[0] != '#' && line[0] != '\n' && line[0] != '\r' && std::strncmp(line, "date=", 5) != 0) count++;
        if (f) std::fclose(f);
    }
    std::string dir = target.substr(0, target.find_last_of('/'));
    mkdir(dir.c_str(), 0777);
    if (p.replace) {
        if (p.kind == "airspace") removeOthers(dir, ".txt", target);
        if (p.kind == "waypoints" || p.kind == "route") removeOthers(dir, ".cup", target);
    }
    std::remove(target.c_str());
    if (std::rename(p.tmpPath.c_str(), target.c_str()) != 0) {
        msg = "nie mogę zapisać pliku na karcie";
        return false;
    }
    msg = target.substr(root.size() + 1);
    return true;
}

}  // namespace nav
