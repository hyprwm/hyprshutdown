#pragma once

#include <string>
#include <vector>
#include <regex>
#include <optional>

struct SShutdownRule {
    int                 layer        = 1;
    float               forceTimeout = -1.0F;

    std::string         classPattern;
    std::string         titlePattern;
    std::string         namePattern;
    std::string         cmdlinePattern;
    std::string         pathPattern;
    std::string         userPattern;
    std::string         pidPattern;

    std::regex          regexClass;
    std::regex          regexTitle;
    std::regex          regexName;
    std::regex          regexCmdline;
    std::regex          regexPath;
    std::regex          regexUser;
    std::regex          regexPid;

    bool                hasClass   = false;
    bool                hasTitle   = false;
    bool                hasName    = false;
    bool                hasCmdline = false;
    bool                hasPath    = false;
    bool                hasUser    = false;
    bool                hasPid     = false;
    std::optional<bool> hidden;
};

class CConfig {
  public:
    CConfig() = default;
    ~CConfig() = default;

    void load(const std::string& pathOverride);

    // General settings
    std::string lineColor;
    int         rowMargin       = 14;
    int         lineWidth       = 1;
    bool        hideProcesses   = false;
    bool        systemdUserExit = false;

    // Default block settings
    float               defaultForceTimeout = 5.0F;
    int                 defaultLayer        = -1;
    bool                defaultHidden       = false;

    // Parsed rules
    std::vector<SShutdownRule> rules;

  private:
    std::string getConfigPath(const std::string& pathOverride);
};
