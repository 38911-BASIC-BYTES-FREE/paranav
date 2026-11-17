// Desktop stand-in for the device side of the phone upload protocol (used by the Java protocol test).
// stdin:  "C <text>"  control line | "D <hex>" data packet | "X" disconnect
// stdout: "N <text>"  notification
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "filexfer.h"

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "/tmp/xfer_root";
    nav::FileReceiver rx(root);
    rx.setFirmware("sim");
    rx.setNotify([](const std::string& s) { std::printf("N %s\n", s.c_str()); std::fflush(stdout); });
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.size() >= 2 && line[0] == 'C') rx.onCtrl(line.substr(2));
        else if (line.size() >= 2 && line[0] == 'D') {
            std::vector<uint8_t> b;
            for (size_t i = 2; i + 1 < line.size(); i += 2) b.push_back((uint8_t)std::stoi(line.substr(i, 2), nullptr, 16));
            rx.onData(b.data(), b.size());
        } else if (line == "X") rx.onDisconnect();
        nav::FileReceiver::Pending p;
        if (rx.takePending(p)) {  // what the firmware main loop does
            if (p.kind == "reload") { std::printf("N OK RELOADED\n"); std::fflush(stdout); continue; }
            std::string msg;
            int count;
            bool ok = nav::FileReceiver::install(root, p, msg, count);
            if (ok) std::printf("N OK END %d %s\n", count, msg.c_str());
            else std::printf("N ERR %s\n", msg.c_str());
            std::fflush(stdout);
        }
    }
}
