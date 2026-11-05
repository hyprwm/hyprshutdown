#include "AppState.hpp"
#include "HyprlandIPC.hpp"
#include "../helpers/Logger.hpp"
#include "../helpers/OS.hpp"

#include <algorithm>
#include <ranges>
#include <csignal>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <functional>

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
        if (m_address.empty()) {
            g_logger->log(LOG_WARN, "CApp::quit: app {} has no address and no valid pid, skipping", m_class);
            return;
        }
        g_logger->log(LOG_TRACE, "CApp::quit: using close for {}", m_class);
        std::string cmd;
        if (State::state()->m_useLua)
            cmd = std::format("/dispatch hl.dsp.window.close({{ window = 'address:{}' }})", m_address);
        else
            cmd = std::format("/dispatch closewindow address:{}", m_address);
        auto ret = HyprlandIPC::getFromSocket(cmd);
        if (!ret)
            g_logger->log(LOG_ERR, "Failed closing window {}: ipc err", m_class);
        else if (*ret != "ok")
            g_logger->log(LOG_ERR, "Failed closing window {}: {}", m_class, *ret);
    } else {
        // SIGTERM with pid
        if (m_pid <= 0) {
            g_logger->log(LOG_WARN, "CApp::quit: app {} has invalid pid {}, skipping SIGTERM", m_class, m_pid);
            return;
        }
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
    if (State::state()->m_dryRun && m_quitSent) {
        auto  elapsed       = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_quitTime).count() / 1000.F;
        float simulatedTime = 2.0F;
        if (m_forceTimeout >= 0.F) {
            simulatedTime = m_forceTimeout;
        }
        if (m_forceTimeout == 0.0F) {
            simulatedTime = 0.5F;
        }
        if (elapsed >= simulatedTime) {
            return false;
        }
    }

    if (!m_address.empty()) {
        return m_windowPresent;
    }

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

    // detect config provider
    {
        const auto RET = HyprlandIPC::getFromSocket("j/status");
        if (RET) {
            auto jsonRaw = glz::read_json<glz::generic>(*RET);
            if (jsonRaw && jsonRaw->get_object().contains("configProvider")) {
                std::string provider = jsonRaw->get_object()["configProvider"].get_string();
                m_useLua             = (provider == "lua");
                g_logger->log(LOG_DEBUG, "Detected config provider: {}", m_useLua ? "lua" : "hyprlang");
            }
        }
    }

    // load config and setup rules
    loadConfig();

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

    // Classify all apps
    for (auto& app : m_apps) {
        classifyApp(*app);
    }

    // Determine the unique stages present
    std::vector<int> firstLayers;
    std::vector<int> lastLayers;
    bool             hasNormal = false;

    for (const auto& app : m_apps) {
        if (app->m_category == EAppCategory::FIRST) {
            if (std::ranges::find(firstLayers, app->m_layer) == firstLayers.end()) {
                firstLayers.push_back(app->m_layer);
            }
        } else if (app->m_category == EAppCategory::LAST) {
            if (std::ranges::find(lastLayers, app->m_layer) == lastLayers.end()) {
                lastLayers.push_back(app->m_layer);
            }
        } else if (app->m_category == EAppCategory::NORMAL) {
            hasNormal = true;
        }
    }

    std::ranges::sort(firstLayers, std::greater<>{});
    std::ranges::sort(lastLayers);

    m_stages.clear();
    for (int layerIndex : firstLayers) {
        m_stages.push_back({.category = EAppCategory::FIRST, .layer = layerIndex});
    }
    if (hasNormal || m_stages.empty()) {
        m_stages.push_back({.category = EAppCategory::NORMAL, .layer = 0});
    }
    for (int layerIndex : lastLayers) {
        m_stages.push_back({.category = EAppCategory::LAST, .layer = layerIndex});
    }

    auto getStageIndex = [&](const CApp& app) -> int {
        int index = 0;
        for (const auto& stage : m_stages) {
            if (app.m_category == stage.category && app.m_layer == stage.layer) {
                return index;
            }
            index++;
        }
        return static_cast<int>(m_stages.size());
    };

    auto getTypeIndex = [](const CApp& app) -> int {
        if (!app.isProcess() && !app.isLayer()) {
            return 0; // Windows
        }
        if (app.isLayer()) {
            return 1; // Wayland layers
        }
        return 2; // Programs/processes
    };

    std::ranges::stable_sort(m_apps, [&](const UP<CApp>& a, const UP<CApp>& b) {
        int idxA = getStageIndex(*a);
        int idxB = getStageIndex(*b);
        if (idxA != idxB) {
            return idxA < idxB;
        }
        int typeA = getTypeIndex(*a);
        int typeB = getTypeIndex(*b);
        if (typeA != typeB) {
            return typeA < typeB;
        }
        return a->m_class < b->m_class;
    });

    for (auto& app : m_apps) {
        app->m_hasWindow = false;
        if (app->isProcess()) {
            for (const auto& other : m_apps) {
                if (!other->isProcess() && other->m_pid == app->m_pid && other->m_pid > 0) {
                    app->m_hasWindow = true;
                    break;
                }
            }
        }
    }

    m_stageIndex   = 0;
    m_stageStarted = std::chrono::steady_clock::now();

    g_logger->log(LOG_DEBUG, "Initialized {} shutdown stages", m_stages.size());

    // Trigger quit for the first stage
    if (!m_stages.empty()) {
        const auto& currentStage = m_stages.at(m_stageIndex);
        g_logger->log(LOG_DEBUG, "Starting stage: {}", stageName(currentStage));
        for (const auto& app : m_apps) {
            if (isAppInStage(*app, currentStage)) {
                g_logger->log(LOG_DEBUG, "Starting quit for app in stage {}: {}", stageName(currentStage), app->m_class);
                startAppQuit(*app);
            }
        }
    }

    checkStageTransition();

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

    auto table = jsonRaw->get_array();

    for (auto& app : m_apps) {
        if (!app->m_address.empty()) {
            app->m_windowPresent = std::ranges::any_of(table, [&app](const auto& te) { return te == *app; });
        }
    }

    const auto BEFORE = m_apps.size();

    std::erase_if(m_apps, [&table](const auto& e) { return !e->appAlive() && !std::ranges::any_of(table, [&e](const auto& te) { return te == *e; }); });

    // check PIDs
    if (!m_dryRun) {
        for (const auto& app : m_apps) {
            if (!app->appAlive() || app->m_pid <= 0 || app->m_address.empty() /* not a window */ || std::ranges::contains(m_pidsTermedNoWindows, app->m_pid))
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
    }

    g_logger->log(LOG_DEBUG, "Updated state: apps size {}", m_apps.size());

    // check force timeouts
    for (const auto& app : m_apps) {
        if (app->appAlive() && app->m_quitSent && app->m_forceTimeout >= 0.0F) {
            float elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - app->m_quitTime).count() / 1000.F;
            if (elapsed >= app->m_forceTimeout) {
                g_logger->log(LOG_WARN, "Force-quit timeout exceeded for {} ({}s). Force-killing.", app->m_class, app->m_forceTimeout);
                if (!m_dryRun) {
                    app->kill();
                }
                app->m_forceTimeout = -1.0F;
            }
        }
    }

    for (auto& app : m_apps) {
        app->m_hasWindow = false;
        if (app->isProcess()) {
            for (const auto& other : m_apps) {
                if (!other->isProcess() && other->m_pid == app->m_pid && other->m_pid > 0) {
                    app->m_hasWindow = true;
                    break;
                }
            }
        }
    }

    checkStageTransition();

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
        if (a->m_quitSent) {
            a->quit();
        }
    }
}

void CAppState::forceCurrentStage() {
    if (m_stages.empty() || m_stageIndex >= m_stages.size())
        return;

    const auto& currentStage = m_stages.at(m_stageIndex);
    g_logger->log(LOG_DEBUG, "Forcing stage: {}", stageName(currentStage));
    for (const auto& app : m_apps) {
        if (app->appAlive() && isAppInStage(*app, currentStage)) {
            if (!m_dryRun) {
                app->kill();
            }
        }
    }
    advanceStage();
}

void CAppState::loadConfig() {
    CConfig config;
    config.load(m_configPathOverride);

    m_lineColor = config.lineColor;
    m_rowMargin = config.rowMargin;
    m_lineWidth = config.lineWidth;
    m_hideProcesses = config.hideProcesses;
    m_systemdUserExit = config.systemdUserExit;
    m_defaultForceTimeout = config.defaultForceTimeout;
    m_defaultLayer = config.defaultLayer;
    m_defaultHidden = config.defaultHidden;
    m_rules = std::move(config.rules);
}

void CAppState::classifyApp(CApp& app) {
    if (m_defaultLayer == 0) {
        app.m_category = EAppCategory::LAST;
        app.m_layer    = 0;
    } else if (m_defaultLayer > 0) {
        app.m_category = EAppCategory::FIRST;
        app.m_layer    = m_defaultLayer;
    } else {
        app.m_category = EAppCategory::NORMAL;
        app.m_layer    = 0;
    }
    app.m_forceTimeout = m_defaultForceTimeout;
    app.m_hidden       = m_defaultHidden;
    if (app.isProcess() && m_hideProcesses) {
        app.m_hidden = true;
    }

    for (const auto& rule : m_rules) {
        if (matchRule(app, rule)) {
            if (rule.layer == 0) {
                app.m_category = EAppCategory::LAST;
            } else {
                app.m_category = EAppCategory::FIRST;
            }
            app.m_layer        = rule.layer;
            app.m_forceTimeout = rule.forceTimeout;
            if (rule.hidden.has_value()) {
                app.m_hidden = rule.hidden.value();
            }
            break;
        }
    }
}

bool CAppState::matchRule(const CApp& app, const SShutdownRule& rule) {
    if (rule.hasClass) {
        try {
            if (!std::regex_match(app.m_class, rule.regexClass) && !std::regex_search(app.m_class, rule.regexClass))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasTitle) {
        try {
            if (!std::regex_match(app.m_title, rule.regexTitle) && !std::regex_search(app.m_title, rule.regexTitle))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasName) {
        std::string valueToMatch;
        if (app.m_pid > 0) {
            valueToMatch = OS::appNameForPid(app.m_pid);
        }
        if (valueToMatch.empty()) {
            valueToMatch = app.m_class;
        }
        try {
            if (!std::regex_match(valueToMatch, rule.regexName) && !std::regex_search(valueToMatch, rule.regexName))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasCmdline) {
        std::string valueToMatch;
        if (app.m_pid > 0) {
            valueToMatch = OS::cmdLineForPid(app.m_pid);
        }
        try {
            if (!std::regex_match(valueToMatch, rule.regexCmdline) && !std::regex_search(valueToMatch, rule.regexCmdline))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasPath) {
        std::string valueToMatch;
        if (app.m_pid > 0) {
            valueToMatch = OS::exePathForPid(app.m_pid);
        }
        try {
            if (!std::regex_match(valueToMatch, rule.regexPath) && !std::regex_search(valueToMatch, rule.regexPath))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasUser) {
        std::string valueToMatch;
        if (app.m_pid > 0) {
            valueToMatch = OS::userForPid(app.m_pid);
        }
        try {
            if (!std::regex_match(valueToMatch, rule.regexUser) && !std::regex_search(valueToMatch, rule.regexUser))
                return false;
        } catch (...) { return false; }
    }
    if (rule.hasPid) {
        std::string valueToMatch = std::to_string(app.m_pid);
        try {
            if (!std::regex_match(valueToMatch, rule.regexPid) && !std::regex_search(valueToMatch, rule.regexPid))
                return false;
        } catch (...) { return false; }
    }

    return rule.hasClass || rule.hasTitle || rule.hasName || rule.hasCmdline || rule.hasPath || rule.hasUser || rule.hasPid;
}

void CAppState::checkStageTransition() {
    if (m_stages.empty() || m_stageIndex >= m_stages.size()) {
        return;
    }

    const auto& currentStage           = m_stages.at(m_stageIndex);
    bool        hasAliveInCurrentStage = false;
    for (const auto& app : m_apps) {
        if (app->appAlive() && isAppInStage(*app, currentStage)) {
            hasAliveInCurrentStage = true;
            break;
        }
    }

    float stageSecs      = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_stageStarted).count() / 1000.F;
    float currentTimeout = getTimeoutForStage(currentStage);

    bool  timeoutExpired = (stageSecs >= currentTimeout);

    if (!hasAliveInCurrentStage || timeoutExpired) {
        if (timeoutExpired && hasAliveInCurrentStage) {
            g_logger->log(LOG_WARN, "Stage {} timeout expired ({}s). Proceeding to next stage.", stageName(currentStage), currentTimeout);
        }
        advanceStage();
    }
}

void CAppState::startAppQuit(CApp& app) {
    app.m_quitSent = true;
    app.m_quitTime = std::chrono::steady_clock::now();
    if (app.m_forceTimeout == 0.0F) {
        g_logger->log(LOG_DEBUG, "Force-killing app immediately: {}", app.m_class);
        if (!m_dryRun) {
            app.kill();
        }
    } else {
        if (!m_dryRun) {
            app.quit();
        }
    }
}

void CAppState::advanceStage() {
    if (m_stages.empty() || m_stageIndex >= m_stages.size()) {
        return;
    }

    m_stageIndex++;
    if (m_stageIndex >= m_stages.size()) {
        g_logger->log(LOG_DEBUG, "All shutdown stages complete");
        return;
    }

    m_stageStarted        = std::chrono::steady_clock::now();
    const auto& nextStage = m_stages.at(m_stageIndex);
    g_logger->log(LOG_DEBUG, "Transitioning to stage: {}", stageName(nextStage));

    for (const auto& app : m_apps) {
        if (isAppInStage(*app, nextStage)) {
            g_logger->log(LOG_DEBUG, "Starting quit for app in stage {}: {}", stageName(nextStage), app->m_class);
            startAppQuit(*app);
        }
    }

    checkStageTransition();
}

bool CAppState::isAppInStage(const CApp& app, const SShutdownStage& stage) const {
    return app.m_category == stage.category && app.m_layer == stage.layer;
}

float CAppState::getTimeoutForStage(const SShutdownStage& stage) const {
    float maxTimeout   = 0.F;
    bool  hasUnlimited = false;
    bool  hasApps      = false;

    for (const auto& app : m_apps) {
        if (app->appAlive() && isAppInStage(*app, stage)) {
            hasApps = true;
            if (app->m_forceTimeout < 0.F) {
                hasUnlimited = true;
            } else {
                maxTimeout = std::max(maxTimeout, app->m_forceTimeout);
            }
        }
    }

    if (hasApps) {
        if (hasUnlimited) {
            return 999999.F; // Effectively unlimited
        }
        return std::max(maxTimeout, 1.0F);
    }

    switch (stage.category) {
        case EAppCategory::FIRST: return m_timeoutFirst;
        case EAppCategory::NORMAL: return m_timeoutNormal;
        case EAppCategory::LAST: return m_timeoutLast;
        default: return 0.F;
    }
}

std::string CAppState::stageName(const SShutdownStage& stage) const {
    std::string catName;
    switch (stage.category) {
        case EAppCategory::FIRST: catName = "FIRST"; break;
        case EAppCategory::NORMAL: catName = "NORMAL"; break;
        case EAppCategory::LAST: catName = "LAST"; break;
        default: catName = "UNKNOWN"; break;
    }
    return std::format("{}(layer {})", catName, stage.layer);
}
