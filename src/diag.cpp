#include "diag.h"

#include <Arduino.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "i18n.h"

namespace diag {

static constexpr uint32_t kMagic = 0x44494147;  // "DIAG"
static constexpr int kCrumbs = 12, kCrumbLen = 60;

struct Rtc {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t unexpectedInRow;   // unexpected restarts in a row (each after a short run)
    uint32_t head;              // next crumb slot
    char crumb[kCrumbs][kCrumbLen];
    uint32_t crumbMs[kCrumbs];
    uint16_t crumbRepeat[kCrumbs];
    Snapshot snap;
    uint32_t heapMinKb, psramMinKb;
};
RTC_NOINIT_ATTR static Rtc rtc;

static Rtc prev;     // copy of the previous run (for the report)
static bool prevValid = false;
static Boot info;

const char* reasonText(int reason) {
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_POWERON: return TR("włączenie zasilania", "power on");
        case ESP_RST_EXT: return TR("przycisk RST", "reset pin");
        case ESP_RST_SW: return TR("restart programowy (np. po wgraniu firmware)", "software restart (e.g. after a firmware upload)");
        case ESP_RST_PANIC: return TR("błąd programu (panic)", "software error (panic)");
        case ESP_RST_INT_WDT: return TR("zawieszenie (watchdog przerwań)", "hang (interrupt watchdog)");
        case ESP_RST_TASK_WDT: return TR("zawieszenie (watchdog zadań)", "hang (task watchdog)");
        case ESP_RST_WDT: return TR("zawieszenie (watchdog)", "hang (watchdog)");
        case ESP_RST_DEEPSLEEP: return TR("wybudzenie", "wake-up");
        case ESP_RST_BROWNOUT: return TR("spadek napięcia (bateria?)", "brownout (battery?)");
        default: return TR("nieznany", "unknown");
    }
}

const Boot& begin() {
    esp_reset_reason_t r = esp_reset_reason();
    prevValid = rtc.magic == kMagic && r != ESP_RST_POWERON;
    if (prevValid) {
        prev = rtc;
        for (auto& c : prev.crumb) c[kCrumbLen - 1] = 0;
    } else {
        std::memset(&rtc, 0, sizeof rtc);
        rtc.magic = kMagic;
    }
    info.reason = (int)r;
    info.crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
                 r == ESP_RST_BROWNOUT;
    info.unexpected = info.crash || r == ESP_RST_SW;
    rtc.bootCount++;
    info.bootCount = rtc.bootCount;
    if (prevValid) {
        info.prevUptimeS = prev.snap.uptimeS;
        int last = (int)((prev.head + kCrumbs - 1) % kCrumbs);
        info.lastAction = prev.crumb[last];
    }
    // safe mode: 3rd crash in a row, each one within 2 minutes of starting
    if (info.crash && info.prevUptimeS < 120) rtc.unexpectedInRow++;
    else rtc.unexpectedInRow = 0;
    info.safeMode = rtc.unexpectedInRow >= 3;
    // start a fresh breadcrumb trail for this run
    std::memset(rtc.crumb, 0, sizeof rtc.crumb);
    std::memset(rtc.crumbMs, 0, sizeof rtc.crumbMs);
    std::memset(rtc.crumbRepeat, 0, sizeof rtc.crumbRepeat);
    rtc.head = 0;
    rtc.snap = Snapshot();
    rtc.heapMinKb = rtc.psramMinKb = 0;
    return info;
}

const Boot& boot() { return info; }

void crumb(const char* s) {
    uint32_t last = (rtc.head + kCrumbs - 1) % kCrumbs;
    if (rtc.crumbMs[last] && std::strncmp(rtc.crumb[last], s, kCrumbLen - 1) == 0) {
        rtc.crumbMs[last] = millis();  // same action again: keep one line with a counter
        if (rtc.crumbRepeat[last] < 65535) rtc.crumbRepeat[last]++;
        return;
    }
    std::strncpy(rtc.crumb[rtc.head], s, kCrumbLen - 1);
    rtc.crumb[rtc.head][kCrumbLen - 1] = 0;
    rtc.crumbMs[rtc.head] = millis() | 1;
    rtc.crumbRepeat[rtc.head] = 0;
    rtc.head = (rtc.head + 1) % kCrumbs;
}

void tick(const Snapshot& s) {
    rtc.snap = s;
    rtc.heapMinKb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
    rtc.psramMinKb = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024;
    if (s.uptimeS > 300) rtc.unexpectedInRow = 0;  // ran fine for 5 minutes: leave safe mode next time
}

static void appendLine(const char* path, const std::string& s) {
    FILE* f = std::fopen(path, "a");
    if (!f) return;
    std::fputs(s.c_str(), f);
    std::fclose(f);
}

void noteShutdown(const char* logsDir) {
    char line[96];
    std::snprintf(line, sizeof line, "shutdown  %s %u s\n", TR("wyłączenie przez użytkownika po", "powered off by the user after"),
                  (unsigned)(millis() / 1000));
    appendLine((std::string(logsDir) + "/restarts.txt").c_str(), line);
}

// Last line of restarts.txt (the file grows by one short line per boot / power off)
static std::string lastLine(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    long from = size > 600 ? size - 600 : 0;
    std::fseek(f, from, SEEK_SET);
    char buf[640];
    size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    char* nl = std::strrchr(buf, '\n');
    return nl ? nl + 1 : buf;
}

std::string writeReports(const char* logsDir, const char* version) {
    mkdir(logsDir, 0777);
    // power-on after a run that never powered off properly = the power was cut (battery? cable?)
    std::string prevLine = lastLine(std::string(logsDir) + "/restarts.txt");
    if (info.reason == ESP_RST_POWERON && prevLine.rfind("boot ", 0) == 0) {
        info.powerLoss = true;
        info.unexpected = true;
    }
    char line[300];
    std::snprintf(line, sizeof line, "boot %u  fw %s  %s: %s", (unsigned)info.bootCount, version,
                  TR("powód", "reason"), reasonText(info.reason));
    std::string l = line;
    if (prevValid) {
        std::snprintf(line, sizeof line, "  |  %s %u s, %s %.2f V", TR("poprzednio działał", "previous run"),
                      (unsigned)prev.snap.uptimeS, TR("bateria", "battery"), (double)prev.snap.battV);
        l += line;
        if (!info.lastAction.empty())
            std::snprintf(line, sizeof line, ", %s: %s", TR("ostatnio", "last"), info.lastAction.c_str());
        else
            line[0] = 0;
        l += line;
    }
    if (info.powerLoss) l += TR("  |  ZANIK ZASILANIA (bez wyłączenia)", "  |  POWER LOSS (not powered off)");
    if (info.safeMode) l += TR("  |  TRYB AWARYJNY", "  |  SAFE MODE");
    appendLine((std::string(logsDir) + "/restarts.txt").c_str(), l + "\n");

    bool dump = esp_core_dump_image_check() == ESP_OK;
    if (!info.unexpected && !dump) return "";
    // full report
    char path[64];
    FILE* f = nullptr;
    for (int n = 1; n < 1000 && !f; n++) {
        std::snprintf(path, sizeof path, "%s/crash-%02d.txt", logsDir, n);
        FILE* t = std::fopen(path, "r");
        if (t) {
            std::fclose(t);
            continue;
        }
        f = std::fopen(path, "w");
    }
    if (!f) return "";
    std::fprintf(f, "ParaNav %s - %s\n", version, TR("raport restartu", "restart report"));
    std::fprintf(f, "%s: %s (%d)\n", TR("powód", "reason"), reasonText(info.reason), info.reason);
    std::fprintf(f, "boot nr: %u%s\n", (unsigned)info.bootCount, info.safeMode ? TR("  (start w trybie awaryjnym)", "  (starting in safe mode)") : "");
    if (prevValid) {
        const Snapshot& s = prev.snap;
        std::fprintf(f, "\n%s:\n", TR("Stan tuż przed restartem", "State just before the restart"));
        std::fprintf(f, "  %s: %u s\n", TR("czas działania", "uptime"), (unsigned)s.uptimeS);
        std::fprintf(f, "  %s: %.2f V, %d %%, USB %s\n", TR("bateria", "battery"), (double)s.battV, s.battPct,
                     s.usb ? TR("tak", "yes") : TR("nie", "no"));
        std::fprintf(f, "  %s: %s, %s: %s, FANET %s (tx %u)\n", TR("ekran", "screen"), s.screen, TR("lot", "flying"),
                     s.flying ? TR("tak", "yes") : TR("nie", "no"), s.fanetOn ? TR("wł.", "on") : TR("wył.", "off"),
                     (unsigned)s.fanetTx);
        std::fprintf(f, "  %s: RAM %u kB, PSRAM %u kB\n", TR("najmniej wolnej pamięci", "lowest free memory"),
                     (unsigned)prev.heapMinKb, (unsigned)prev.psramMinKb);
        std::fprintf(f, "\n%s:\n", TR("Ostatnie czynności (najnowsza na dole)", "Last actions (newest at the bottom)"));
        for (int i = 0; i < kCrumbs; i++) {
            int k = (int)((prev.head + i) % kCrumbs);
            if (!prev.crumbMs[k]) continue;
            std::fprintf(f, "  %8.1f s  %s", prev.crumbMs[k] / 1000.0, prev.crumb[k]);
            if (prev.crumbRepeat[k]) std::fprintf(f, "  (x%u)", (unsigned)prev.crumbRepeat[k] + 1);
            std::fputc('\n', f);
        }
    } else {
        std::fprintf(f, "\n%s\n", TR("Brak danych z pamięci RTC (np. po zaniku zasilania).", "No data in RTC memory (e.g. after a power loss)."));
    }
    if (info.powerLoss)
        std::fprintf(f, "\n%s\n%s: %s\n",
                     TR("Urządzenie straciło zasilanie bez wyłączenia (rozładowana bateria, styk baterii, "
                        "odcięcie przez układ ładowania?).", "The device lost power without being powered off "
                        "(flat battery, battery contact, cut by the charger chip?)."),
                     TR("Poprzedni wpis", "Previous entry"), prevLine.c_str());
    if (dump) {
        esp_core_dump_summary_t* sm = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
        if (sm && esp_core_dump_get_summary(sm) == ESP_OK) {
            std::fprintf(f, "\nCore dump: %s %s, PC 0x%08x, %s %u, %s 0x%08x\n", TR("zadanie", "task"), sm->exc_task,
                         (unsigned)sm->exc_pc, TR("przyczyna", "cause"), (unsigned)sm->ex_info.exc_cause,
                         TR("adres", "address"), (unsigned)sm->ex_info.exc_vaddr);
            std::string bt;
            for (uint32_t i = 0; i < sm->exc_bt_info.depth && i < 16; i++) {
                char b[12];
                std::snprintf(b, sizeof b, "0x%08x ", (unsigned)sm->exc_bt_info.bt[i]);
                bt += b;
            }
            std::fprintf(f, "backtrace: %s%s\n", bt.c_str(), sm->exc_bt_info.corrupted ? " (corrupted)" : "");
            std::fprintf(f, "\n%s: xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/t5s3pro/firmware.elf %s\n",
                         TR("Dekodowanie", "Decoding"), bt.c_str());
        }
        free(sm);
        esp_core_dump_image_erase();  // report once
    } else if (info.crash) {
        std::fprintf(f, "\n%s\n", info.reason == ESP_RST_BROWNOUT
                                     ? TR("Brak zrzutu pamięci: przy spadku napięcia procesor nie zdąży go zapisać.",
                                          "No core dump: on a brownout the processor has no time to save one.")
                                     : TR("Brak zrzutu pamięci (nie udało się go zapisać).", "No core dump (could not be saved)."));
    }
    std::fclose(f);
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

}  // namespace diag
