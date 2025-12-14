#include "AppState.hpp"
#include "HyprlandIPC.hpp"
#include "../helpers/Logger.hpp"
#include "../helpers/OS.hpp"

#include <algorithm>
#include <ranges>
#include <csignal>

#include <hyprutils/string/String.hpp>

using namespace State;

static const std::vector<const char*> IGNORE_DAEMONS = {
    "Xwayland",
};

SP<CAppState> State::state() {
    static auto state = makeShared<CAppState>();
    return state;
}

CApp::CApp(glz::generic::object_t& object) {
    if (object.contains("address"))
        m_address = object["address"].get_string();
    if (object.contains("title"))
        m_title = object["title"].get_string();
    if (object.contains("class"))
        m_class = object["class"].get_string();
    if (object.contains("namespace")) {
        m_class        = object["namespace"].get_string();
        m_alwaysUsePid = true; // layers cant be closewindow'd
    }
    if (object.contains("xwayland"))
        m_xwayland = object["xwayland"].get_boolean();
    if (object.contains("pid"))
        m_pid = sc<int64_t>(object["pid"].get_number());
}

CApp::CApp(const std::string& name, int pid) : m_class(name), m_pid(pid), m_alwaysUsePid(true) {
    ;
}

void CApp::quit() {
    if (!m_alwaysUsePid && (!m_address.empty() || m_pid <= 0)) {
        // for apps that have an address, use closewindow. Some apps don't ask for saving on SIGTERM
        g_logger->log(LOG_TRACE, "CApp::quit: using close for {}", m_class);
        auto ret = HyprlandIPC::getFromSocket(std::format("/dispatch closewindow address:{}", m_address));
        if (!ret)
            g_logger->log(LOG_ERR, "Failed closing window {}: ipc err", m_class);

        if (*ret != "ok")
            g_logger->log(LOG_ERR, "Failed closing window {}: {}", m_class, *ret);
    } else {
        // SIGTERM with pid
        g_logger->log(LOG_TRACE, "CApp::quit: using SIGTERM for {}, pid {}", m_class, m_pid);
        if (::kill(m_pid, SIGTERM) != 0)
            g_logger->log(LOG_ERR, "CApp::quit: signal failed for pid {}, err: {}", m_pid, strerror(errno));
    }
}

void CApp::kill() {
    if (m_pid <= 0) {
        g_logger->log(LOG_TRACE, "Can't kill {}: no pid", m_class);
        return;
    }

    g_logger->log(LOG_TRACE, "CApp::kill: killing {}, pid {}", m_class, m_pid);
    if (::kill(m_pid, SIGKILL) != 0)
        g_logger->log(LOG_ERR, "CApp::quit: signal failed for pid {}, err: {}", m_pid, strerror(errno));
}

bool CApp::appAlive() const {
    if (m_pid <= 0)
        return false;

    if (::kill(m_pid, 0) == 0)
        return true;

    if (errno == EPERM)
        return true;

    return false;
}

bool CApp::operator==(const glz::generic& object) const {
    if (!object.contains("address"))
        return false;

    return m_address == object["address"].get_string();
}

bool CAppState::init() {

    // windows
    {
        const auto RET = HyprlandIPC::getFromSocket("j/clients");

        if (!RET) {
            g_logger->log(LOG_ERR, "Couldn't get clients from socket");
            return false;
        }

        auto jsonRaw = glz::read_json<glz::generic>(*RET);

        if (!jsonRaw) {
            g_logger->log(LOG_ERR, "Socket returned bad data");
            return false;
        }

        auto jsonArr = jsonRaw->get_array();

        m_apps.reserve(jsonArr.size());

        for (auto& el : jsonArr) {
            m_apps.emplace_back(makeUnique<CApp>(el.get_object()));
        }
    }

    // layers
    {
        const auto RET = HyprlandIPC::getFromSocket("j/layers");

        if (!RET) {
            g_logger->log(LOG_ERR, "Couldn't get layers from socket");
            return false;
        }

        auto jsonRaw = glz::read_json<glz::generic>(*RET);

        if (!jsonRaw) {
            g_logger->log(LOG_ERR, "Socket returned bad data");
            return false;
        }

        for (auto& [m, obj] : jsonRaw->get_object()) {
            for (auto& [m2, obj2] : obj["levels"].get_object()) {
                for (auto& el : obj2.get_array()) {
                    m_apps.emplace_back(makeUnique<CApp>(el.get_object()));
                }
            }
        }

        g_logger->log(LOG_DEBUG, "Parsed {} apps from socket", m_apps.size());
    }

    // children of the Hyprland process
    // TODO: make a kernel cgroup in hl. This can miss things.
    // Maybe keep this for BSDs, which don't do cgroups, once we figure out PPid on BSDs.
    {
        const auto INSTANCES = HyprlandIPC::instances();
        const auto HIS       = getenv("HYPRLAND_INSTANCE_SIGNATURE");

        if (HIS && HIS[0] != '\0') {

            const HyprlandIPC::SInstanceData* instance = nullptr;

            for (const auto& I : INSTANCES) {
                if (I.id != HIS)
                    continue;

                instance = &I;
                break;
            }

            if (!instance)
                g_logger->log(LOG_ERR, "Can't get children: no instance??");
            else {
                // get all processes that have a PPid of us
                const auto PROCS = OS::getAllPids();

                for (const auto& pid : PROCS) {

                    // check if child
                    if (OS::ppidOf(pid) != instance->pid)
                        continue;

                    const auto NAME = OS::appNameForPid(pid);

                    if (std::ranges::contains(IGNORE_DAEMONS, NAME))
                        continue;

                    m_apps.emplace_back(makeUnique<CApp>(NAME, pid));
                }
            }

        } else
            g_logger->log(LOG_ERR, "Can't get children: no HIS");
    }

    // exit them if not dry run
    if (!m_dryRun) {
        for (const auto& e : m_apps) {
            e->quit();
        }
    }

    return true;
}

const std::vector<UP<CApp>>& CAppState::apps() const {
    return m_apps;
}

float CAppState::secondsPassed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_started).count() / 1000.F;
}

bool CAppState::updateState() {
    const auto RET = HyprlandIPC::getFromSocket("j/clients");

    if (!RET) {
        g_logger->log(LOG_ERR, "Couldn't get clients from socket");
        return false;
    }

    auto jsonRaw = glz::read_json<glz::generic>(*RET);

    if (!jsonRaw) {
        g_logger->log(LOG_ERR, "Socket returned bad data");
        return false;
    }

    auto       table = jsonRaw->get_array();

    const auto BEFORE = m_apps.size();

    std::erase_if(m_apps, [&table](const auto& e) { return !e->appAlive() && !std::ranges::any_of(table, [&e](const auto& te) { return te == *e; }); });

    // check PIDs
    for (const auto& app : m_apps) {
        if (!app->appAlive() || app->m_pid <= 0 || std::ranges::contains(m_pidsTermedNoWindows, app->m_pid))
            continue;

        const bool HAS_ANY_WINDOWS = std::ranges::any_of(table, [&app](const auto& te) {
            if (!te.contains("pid"))
                return false;

            return sc<int>(te["pid"].get_number()) == app->m_pid;
        });

        if (HAS_ANY_WINDOWS)
            continue;

        // app has no windows, but is alive. Send a SIGTERM.
        // TODO: maybe make this also repeat every 5s or so?
        m_pidsTermedNoWindows.emplace_back(app->m_pid);

        g_logger->log(LOG_DEBUG, "App {} with pid {} window was closed, but pid is alive. Sending SIGTERM.", app->m_class, app->m_pid);
        kill(app->m_pid, SIGTERM);
    }

    g_logger->log(LOG_DEBUG, "Updated state: apps size {}", m_apps.size());

    return BEFORE != m_apps.size();
}

void CAppState::killAllApps() const {
    if (m_dryRun) {
        g_logger->log(LOG_TRACE, "CAppState::killAllApps: ignoring, dry run");
        return;
    }

    for (const auto& a : m_apps) {
        a->kill();
    }
}

void CAppState::reexitApps() const {
    if (m_dryRun) {
        g_logger->log(LOG_TRACE, "CAppState::reexitApps: ignoring, dry run");
        return;
    }

    for (const auto& a : m_apps) {
        a->quit();
    }
}
