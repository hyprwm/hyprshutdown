#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace OS {
    std::vector<int64_t> getAllPids();
    std::string          appNameForPid(int64_t pid);
    int64_t              ppidOf(int64_t pid);
    std::string          exePathForPid(int64_t pid);
    std::string          cmdLineForPid(int64_t pid);
    std::string          userForPid(int64_t pid);
};