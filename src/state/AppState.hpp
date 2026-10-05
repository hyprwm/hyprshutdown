#pragma once

#include "../helpers/Memory.hpp"
#include "../helpers/Config.hpp"

#include <glaze/glaze.hpp>

#include <chrono>
#include <cstdint>
#include <regex>

namespace State {
    enum class EAppCategory {
        FIRST,
        NORMAL,
        LAST
    };

    struct SShutdownStage {
        EAppCategory category;
        int          layer;

        bool         operator==(const SShutdownStage& other) const {
            return category == other.category && layer == other.layer;
        }
    };
    class CApp {
      public:
        CApp(glz::generic::object_t& object);
        CApp(const std::string& name, int pid);
        ~CApp() = default;

        CApp(const CApp&) = delete;
        CApp(CApp&)       = delete;
        CApp(CApp&&)      = delete;

        bool                                  appAlive() const;
        bool                                  operator==(const glz::generic& object) const;

        void                                  quit();
        void                                  kill();

        std::string                           m_address;
        std::string                           m_title;
        std::string                           m_class;
        int64_t                               m_pid          = -1;
        bool                                  m_xwayland     = false;
        bool                                  m_alwaysUsePid = false;
        bool                                  m_quitSent     = false;
        EAppCategory                          m_category     = EAppCategory::NORMAL;
        int                                   m_layer        = 0;
        float                                 m_forceTimeout = -1.0F;
        std::chrono::steady_clock::time_point m_quitTime;
        bool                                  m_hidden        = false;
        bool                                  m_hasWindow     = false;
        bool                                  m_windowPresent = true;
        bool                                  isProcess() const {
            return m_address.empty();
        }
        bool isLayer() const {
            return !m_address.empty() && m_alwaysUsePid;
        }
    };

    class CAppState {
      public:
        CAppState()  = default;
        ~CAppState() = default;

        bool m_useLua = false;

        CAppState(const CAppState&) = delete;
        CAppState(CAppState&)       = delete;
        CAppState(CAppState&&)      = delete;

        bool                                  init();
        bool                                  updateState();
        float                                 secondsPassed() const;
        void                                  killAllApps() const;
        void                                  reexitApps() const;
        void                                  forceCurrentStage();

        const std::vector<UP<CApp>>&          apps() const;

        bool                                  m_dryRun = false;
        std::string                           m_configPathOverride;

        float                                 m_timeoutFirst        = 3.0F;
        float                                 m_timeoutNormal       = 5.0F;
        float                                 m_timeoutLast         = 3.0F;
        bool                                  m_systemdUserExit     = false;
        float                                 m_defaultForceTimeout = 5.0F;
        int                                   m_defaultLayer        = -1;
        bool                                  m_defaultHidden       = false;
        std::string                           m_lineColor;
        int                                   m_rowMargin     = 14;
        int                                   m_lineWidth     = 1;
        bool                                  m_hideProcesses = false;

        std::vector<SShutdownStage>           m_stages;
        size_t                                m_stageIndex = 0;
        std::chrono::steady_clock::time_point m_stageStarted;

        void                                  loadConfig();
        void                                  classifyApp(CApp& app);
        bool                                  matchRule(const CApp& app, const SShutdownRule& rule);
        void                                  startAppQuit(CApp& app);
        void                                  checkStageTransition();
        void                                  advanceStage();
        bool                                  isAppInStage(const CApp& app, const SShutdownStage& stage) const;
        float                                 getTimeoutForStage(const SShutdownStage& stage) const;
        std::string                           stageName(const SShutdownStage& stage) const;

      private:
        std::vector<UP<CApp>>                 m_apps;
        std::vector<int>                      m_pidsTermedNoWindows;
        std::vector<SShutdownRule>            m_rules;

        std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
    };

    SP<CAppState> state();
};
