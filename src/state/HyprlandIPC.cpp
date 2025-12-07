#include "HyprlandIPC.hpp"

#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/signal.h>
#include <unistd.h>

#include <format>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include <hyprutils/memory/Casts.hpp>

using namespace Hyprutils::Memory;

static int getUID() {
    const auto UID   = getuid();
    const auto PWUID = getpwuid(UID);
    return PWUID ? PWUID->pw_uid : UID;
}

static std::string getRuntimeDir() {
    const auto XDG = getenv("XDG_RUNTIME_DIR");

    if (!XDG) {
        const std::string USERID = std::to_string(getUID());
        return "/run/user/" + USERID + "/hypr";
    }

    return std::string{XDG} + "/hypr";
}

static std::optional<uint64_t> toUInt64(const std::string_view str) {
    uint64_t value       = 0;
    const auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec != std::errc() || ptr != str.data() + str.size())
        return std::nullopt;
    return value;
}

std::expected<std::string, std::string> HyprlandIPC::getFromSocket(const std::string& cmd) {
    static const auto HIS = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!HIS || HIS[0] == '\0')
        return std::unexpected("HYPRLAND_INSTANCE_SIGNATURE empty: are we under hyprland?");

    const auto SERVERSOCKET = socket(AF_UNIX, SOCK_STREAM, 0);

    auto       t = timeval{.tv_sec = 5, .tv_usec = 0};
    setsockopt(SERVERSOCKET, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(struct timeval));

    if (SERVERSOCKET < 0)
        return std::unexpected("couldn't open a socket (1)");

    sockaddr_un serverAddress = {0};
    serverAddress.sun_family  = AF_UNIX;

    std::string socketPath = getRuntimeDir() + "/" + HIS + "/.socket.sock";

    strncpy(serverAddress.sun_path, socketPath.c_str(), sizeof(serverAddress.sun_path) - 1);

    if (connect(SERVERSOCKET, rc<sockaddr*>(&serverAddress), SUN_LEN(&serverAddress)) < 0)
        return std::unexpected(std::format("couldn't connect to the hyprland socket at {}", socketPath));

    auto sizeWritten = write(SERVERSOCKET, cmd.c_str(), cmd.length());

    if (sizeWritten < 0)
        return std::unexpected("couldn't write (4)");

    std::string reply        = "";
    char        buffer[8192] = {0};

    sizeWritten = read(SERVERSOCKET, buffer, 8192);

    if (sizeWritten < 0) {
        if (errno == EWOULDBLOCK)
            return std::unexpected("Hyprland IPC didn't respond in time");
        return std::unexpected("couldn't read (5)");
    }

    reply += std::string(buffer, sizeWritten);

    while (sizeWritten == 8192) {
        sizeWritten = read(SERVERSOCKET, buffer, 8192);
        if (sizeWritten < 0) {
            return std::unexpected("couldn't read (5)");
        }
        reply += std::string(buffer, sizeWritten);
    }

    close(SERVERSOCKET);

    return reply;
}

static std::optional<HyprlandIPC::SInstanceData> parseInstance(const std::filesystem::directory_entry& entry) {
    if (!entry.is_directory())
        return std::nullopt;

    const auto    lockPath = entry.path() / "hyprland.lock";
    std::ifstream ifs(lockPath);
    if (!ifs.is_open())
        return std::nullopt;

    HyprlandIPC::SInstanceData data;
    data.id = entry.path().filename().string();

    const auto first = std::string_view{data.id}.find_first_of('_');
    const auto last  = std::string_view{data.id}.find_last_of('_');
    if (first == std::string_view::npos || last == std::string_view::npos || last <= first)
        return std::nullopt;

    auto time = toUInt64(std::string_view{data.id}.substr(first + 1, last - first - 1));
    if (!time)
        return std::nullopt;
    data.time = *time;

    std::string line;
    if (!std::getline(ifs, line))
        return std::nullopt;

    auto pid = toUInt64(std::string_view{line});
    if (!pid)
        return std::nullopt;
    data.pid = *pid;

    if (!std::getline(ifs, data.wlSocket))
        return std::nullopt;

    if (std::getline(ifs, line) && !line.empty())
        return std::nullopt; // more lines than expected

    return data;
}

std::vector<HyprlandIPC::SInstanceData> HyprlandIPC::instances() {
    std::vector<SInstanceData> result;

    std::error_code            ec;
    const auto                 runtimeDir = getRuntimeDir();
    if (!std::filesystem::exists(runtimeDir, ec) || ec)
        return result;

    std::filesystem::directory_iterator it(runtimeDir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
        return result;

    for (const auto& el : it) {
        if (auto instance = parseInstance(el))
            result.emplace_back(std::move(*instance));
    }

    std::erase_if(result, [](const auto& el) { return kill(el.pid, 0) != 0 && errno == ESRCH; });

    std::ranges::sort(result, {}, &SInstanceData::time);

    return result;
}
