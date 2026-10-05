#include "OS.hpp"

#include "../state/AppState.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hyprutils/string/String.hpp>
#include <hyprutils/memory/Casts.hpp>

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#if defined(__DragonFly__)
#include <sys/kinfo.h> // struct kinfo_proc
#elif defined(__FreeBSD__)
#include <sys/user.h> // struct kinfo_proc
#endif

#if defined(__NetBSD__)
#undef KERN_PROC
#define KERN_PROC  KERN_PROC2
#define KINFO_PROC struct kinfo_proc2
#else
#define KINFO_PROC struct kinfo_proc
#endif
#if defined(__DragonFly__)
#define KP_PPID(kp) kp.kp_ppid
#elif defined(__FreeBSD__)
#define KP_PPID(kp) kp.ki_ppid
#else
#define KP_PPID(kp) kp.p_ppid
#endif
#endif

static std::optional<std::string> linuxExtractFromStatus(std::ifstream& ifs, const std::string_view& what) {
    std::string line;
    std::string full = std::string{what} + ":";
    while (std::getline(ifs, line)) {
        if (!line.starts_with(full))
            continue;

        return Hyprutils::String::trim(line.substr(full.size()));
    }

    return std::nullopt;
}

std::string OS::appNameForPid(int64_t pid) {
#if defined(KERN_PROC_PID)
    int        mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, Hyprutils::Memory::sc<int>(pid)};
    KINFO_PROC kp;
    size_t     len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, nullptr, 0) == -1)
        return "";
    if (len == 0)
        return "";

#if defined(__FreeBSD__) || defined(__DragonFly__)
    return kp.ki_comm;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    return kp.p_comm;
#endif
#else
    std::string   dir = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream ifs(dir);
    if (!ifs.good())
        return "";

    auto        data = linuxExtractFromStatus(ifs, "Name");
    std::string name = data.value_or("");

    return name;
#endif
}

std::vector<int64_t> OS::getAllPids() {
    std::vector<int64_t> pids;

#if defined(KERN_PROC_PID)
    int                     mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t                  len    = 0;
    std::vector<kinfo_proc> procs;

    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) == -1)
        return {};

    procs.resize(len / sizeof(kinfo_proc));

    if (sysctl(mib, 4, procs.data(), &len, nullptr, 0) == -1)
        return {};

    pids.reserve(procs.size());
    for (const auto& p : procs) {
        pids.emplace_back(p.ki_pid);
    }
#else
    std::error_code ec;

    if (std::filesystem::exists("/proc/self", ec) && !ec) {
        for (const std::filesystem::path& p : std::filesystem::directory_iterator("/proc/", ec)) {
            if (!std::filesystem::exists(p, ec) || ec)
                continue;

            if (!Hyprutils::String::isNumber(p.stem().string()))
                continue;

            try {
                pids.emplace_back(std::stoll(p.stem()));
            } catch (...) { ; }
        }
    }
#endif

    return pids;
}

int64_t OS::ppidOf(int64_t pid) {
#if defined(KERN_PROC_PID)
    int mib[] = {
        CTL_KERN,           KERN_PROC, KERN_PROC_PID, (int)pid,
#if defined(__NetBSD__) || defined(__OpenBSD__)
        sizeof(KINFO_PROC), 1,
#endif
    };
    u_int      miblen = sizeof(mib) / sizeof(mib[0]);
    KINFO_PROC kp;
    size_t     sz = sizeof(KINFO_PROC);
    if (sysctl(mib, miblen, &kp, &sz, nullptr, 0) != -1)
        return KP_PPID(kp);
#else
    std::string   dir = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream ifs(dir);
    if (!ifs.good())
        return -1;

    auto data = linuxExtractFromStatus(ifs, "PPid");

    if (!data)
        return -1;

    try {
        return std::stoll(*data);
    } catch (std::exception& e) { ; }
#endif

    return -1;
}

std::string OS::exePathForPid(int64_t pid) {
    std::error_code ec;
    auto            path = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);
    if (ec)
        return "";
    std::string pathStr = path.string();
    return pathStr;
}

std::string OS::cmdLineForPid(int64_t pid) {
    std::ifstream ifs("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    if (!ifs.good())
        return "";
    std::string cmdline((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (cmdline.empty())
        return "";
    for (size_t i = 0; i < cmdline.size(); ++i) {
        if (cmdline[i] == '\0') {
            if (i == cmdline.size() - 1) {
                cmdline.erase(i);
            } else {
                cmdline[i] = ' ';
            }
        }
    }
    return cmdline;
}

std::string OS::userForPid(int64_t pid) {
    struct stat st;
    if (::stat(("/proc/" + std::to_string(pid)).c_str(), &st) != 0)
        return "";

    struct passwd* pw = ::getpwuid(st.st_uid);
    if (pw)
        return pw->pw_name;
    return std::to_string(st.st_uid);
}
