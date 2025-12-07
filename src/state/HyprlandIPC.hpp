#pragma once

#include <string>
#include <expected>
#include <cstdint>
#include <vector>

namespace HyprlandIPC {
    struct SInstanceData {
        std::string id;
        uint64_t    time;
        uint64_t    pid;
        std::string wlSocket;
    };

    std::expected<std::string, std::string> getFromSocket(const std::string& cmd);
    std::vector<HyprlandIPC::SInstanceData> instances();
};
