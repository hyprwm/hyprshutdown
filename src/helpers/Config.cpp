#include "Config.hpp"
#include "Logger.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <algorithm>
#include <hyprlang.hpp>

static std::string stripQuotes(std::string str) {
    if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') || (str.front() == '\'' && str.back() == '\''))) {
        return str.substr(1, str.size() - 2);
    }
    return str;
}

std::string CConfig::getConfigPath(const std::string& pathOverride) {
    if (!pathOverride.empty()) {
        return pathOverride;
    }
    std::string configPath;
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig && xdgConfig[0] != '\0') {
        configPath = std::string(xdgConfig) + "/hypr/hyprshutdown.conf";
    } else {
        const char* home = std::getenv("HOME");
        if (home && home[0] != '\0') {
            configPath = std::string(home) + "/.config/hypr/hyprshutdown.conf";
        } else {
            struct passwd* pw = ::getpwuid(getuid());
            if (pw) {
                configPath = std::string(pw->pw_dir) + "/.config/hypr/hyprshutdown.conf";
            }
        }
    }
    return configPath;
}

void CConfig::load(const std::string& pathOverride) {
    std::string configPath = getConfigPath(pathOverride);
    g_logger->log(LOG_DEBUG, "Attempting to load config from {}", configPath);

    std::ifstream ifs(configPath);
    if (!ifs.good()) {
        g_logger->log(LOG_DEBUG, "Config file not found, using default shutdown settings");
        return;
    }
    ifs.close();

    Hyprlang::CConfig config(configPath.c_str(), Hyprlang::SConfigOptions{
        .throwAllErrors = false,
        .allowMissingConfig = true
    });

    // Register general settings
    config.addConfigValue("general:line_color", Hyprlang::STRING{""});
    config.addConfigValue("general:row_margin", Hyprlang::INT{14});
    config.addConfigValue("general:line_width", Hyprlang::INT{1});
    config.addConfigValue("general:hide_processes", Hyprlang::INT{0});
    config.addConfigValue("general:systemd_user_exit", Hyprlang::INT{0});

    // Register default block settings
    config.addConfigValue("default:timeout", Hyprlang::STRING{"5.0"});
    config.addConfigValue("default:layer", Hyprlang::INT{-1});
    config.addConfigValue("default:hidden", Hyprlang::INT{0});
    config.addConfigValue("default:hide", Hyprlang::INT{0});

    // Register layer_N special categories
    std::vector<std::string> layers;
    // Pre-register standard layers 0 to 10
    for (int i = 0; i <= 10; ++i) {
        layers.push_back("layer_" + std::to_string(i));
    }
    // Scan file for any other layer_N definitions
    {
        std::ifstream file(configPath);
        if (file.good()) {
            std::string line;
            std::regex layerRegex(R"(layer_([0-9]+))");
            while (std::getline(file, line)) {
                std::smatch match;
                if (std::regex_search(line, match, layerRegex)) {
                    layers.push_back(match.str(0));
                }
            }
        }
    }
    std::ranges::sort(layers);
    const auto [first, last] = std::ranges::unique(layers);
    layers.erase(first, last);

    for (const auto& layerName : layers) {
        config.addSpecialCategory(layerName.c_str(), Hyprlang::SSpecialCategoryOptions{.key = "block_key", .anonymousKeyBased = false});
        config.addSpecialConfigValue(layerName.c_str(), "block_key", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "class", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "title", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "name", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "cmdline", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "path", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "user", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "pid", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "timeout", Hyprlang::STRING{""});
        config.addSpecialConfigValue(layerName.c_str(), "hidden", Hyprlang::INT{-1});
        config.addSpecialConfigValue(layerName.c_str(), "hide", Hyprlang::INT{-1});
    }

    config.commence();
    auto result = config.parse();
    if (result.error) {
        g_logger->log(LOG_ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());
    }

    auto getString = [&](const char* name) -> std::string {
        const char* val = std::any_cast<Hyprlang::STRING>(config.getConfigValue(name));
        return val ? stripQuotes(val) : "";
    };

    // Retrieve general block settings
    lineColor = getString("general:line_color");
    g_logger->log(LOG_DEBUG, "Config: line_color set to {}", lineColor);

    rowMargin = std::any_cast<Hyprlang::INT>(config.getConfigValue("general:row_margin"));
    g_logger->log(LOG_DEBUG, "Config: row_margin set to {}", rowMargin);

    lineWidth = std::any_cast<Hyprlang::INT>(config.getConfigValue("general:line_width"));
    g_logger->log(LOG_DEBUG, "Config: line_width set to {}", lineWidth);

    hideProcesses = std::any_cast<Hyprlang::INT>(config.getConfigValue("general:hide_processes")) == 1;
    g_logger->log(LOG_DEBUG, "Config: hide_processes set to {}", hideProcesses);

    systemdUserExit = std::any_cast<Hyprlang::INT>(config.getConfigValue("general:systemd_user_exit")) == 1;
    g_logger->log(LOG_DEBUG, "Config: systemd_user_exit set to {}", systemdUserExit);

    // Retrieve default block settings
    std::string defaultTimeoutVal = getString("default:timeout");
    if (defaultTimeoutVal == "unlimited") {
        defaultForceTimeout = -1.0F;
    } else {
        try {
            defaultForceTimeout = std::stof(defaultTimeoutVal);
        } catch (...) {
            g_logger->log(LOG_ERR, "Config error: invalid default timeout: '{}'", defaultTimeoutVal);
        }
    }
    g_logger->log(LOG_DEBUG, "Config: default force timeout set to {}", defaultForceTimeout);

    defaultLayer = std::any_cast<Hyprlang::INT>(config.getConfigValue("default:layer"));
    g_logger->log(LOG_DEBUG, "Config: default layer set to {}", defaultLayer);

    defaultHidden = (std::any_cast<Hyprlang::INT>(config.getConfigValue("default:hidden")) == 1) ||
                    (std::any_cast<Hyprlang::INT>(config.getConfigValue("default:hide")) == 1);
    g_logger->log(LOG_DEBUG, "Config: default hidden set to {}", defaultHidden);

    // Retrieve layer rules
    rules.clear();
    for (const auto& layerName : layers) {
        int layerNum = 0;
        try {
            layerNum = std::stoi(layerName.substr(6));
        } catch (...) {
            continue;
        }

        const auto KEYS = config.listKeysForSpecialCategory(layerName.c_str());
        for (const auto& key : KEYS) {
            SShutdownRule rule;
            rule.layer = layerNum;

            auto getSpecialString = [&](const char* name) -> std::string {
                const char* val = std::any_cast<Hyprlang::STRING>(config.getSpecialConfigValue(layerName.c_str(), name, key.c_str()));
                return val ? stripQuotes(val) : "";
            };

            std::string blockKeyVal = getSpecialString("block_key");
            std::string classVal = getSpecialString("class");
            std::string titleVal = getSpecialString("title");
            std::string nameVal = getSpecialString("name");
            std::string cmdlineVal = getSpecialString("cmdline");
            std::string pathVal = getSpecialString("path");
            std::string userVal = getSpecialString("user");
            std::string pidVal = getSpecialString("pid");

            std::string timeoutVal = getSpecialString("timeout");
            int hiddenVal = std::any_cast<Hyprlang::INT>(config.getSpecialConfigValue(layerName.c_str(), "hidden", key.c_str()));
            int hideVal = std::any_cast<Hyprlang::INT>(config.getSpecialConfigValue(layerName.c_str(), "hide", key.c_str()));

            // Set timeout
            rule.forceTimeout = defaultForceTimeout;
            if (!timeoutVal.empty()) {
                if (timeoutVal == "unlimited") {
                    rule.forceTimeout = -1.0F;
                } else {
                    try {
                        rule.forceTimeout = std::stof(timeoutVal);
                    } catch (...) {
                        g_logger->log(LOG_ERR, "Config error: invalid timeout in layer '{}': '{}'", layerName, timeoutVal);
                    }
                }
            }

            // Set hidden
            if (hiddenVal == 1 || hideVal == 1) {
                rule.hidden = true;
            } else if (hiddenVal == 0 || hideVal == 0) {
                rule.hidden = false;
            } else {
                rule.hidden = std::nullopt;
            }

            auto addPattern = [](const std::string& pattern, std::string& patternOut, std::regex& regexOut, bool& flagOut) {
                if (pattern.empty()) return;
                patternOut = pattern;
                try {
                    regexOut = std::regex(pattern, std::regex_constants::ECMAScript | std::regex_constants::nosubs);
                    flagOut  = true;
                } catch (const std::regex_error& e) {
                    g_logger->log(LOG_ERR, "Config error: invalid regex '{}': {}", pattern, e.what());
                }
            };

            addPattern(classVal, rule.classPattern, rule.regexClass, rule.hasClass);
            addPattern(titleVal, rule.titlePattern, rule.regexTitle, rule.hasTitle);
            addPattern(nameVal, rule.namePattern, rule.regexName, rule.hasName);
            addPattern(cmdlineVal, rule.cmdlinePattern, rule.regexCmdline, rule.hasCmdline);
            addPattern(pathVal, rule.pathPattern, rule.regexPath, rule.hasPath);
            addPattern(userVal, rule.userPattern, rule.regexUser, rule.hasUser);
            addPattern(pidVal, rule.pidPattern, rule.regexPid, rule.hasPid);

            // Fallback match: if no criteria is given, use the blockKeyVal as the window class pattern
            bool hasAnyMatch = rule.hasClass || rule.hasTitle || rule.hasName || rule.hasCmdline || rule.hasPath || rule.hasUser || rule.hasPid;
            if (!hasAnyMatch && !blockKeyVal.empty()) {
                addPattern(blockKeyVal, rule.classPattern, rule.regexClass, rule.hasClass);
            }

            g_logger->log(LOG_DEBUG, "Config rule added for {}: layer={}, forceTimeout={}s, hidden={}",
                          layerName, rule.layer, rule.forceTimeout, rule.hidden.has_value() ? (rule.hidden.value() ? "true" : "false") : "default");
            rules.push_back(std::move(rule));
        }
    }
}
