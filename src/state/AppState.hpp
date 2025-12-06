#pragma once

#include "../helpers/Memory.hpp"

#include <glaze/glaze.hpp>

#include <chrono>
#include <cstdint>

namespace State {
    class CApp {
      public:
        CApp(glz::generic::object_t& object);
        ~CApp() = default;

        CApp(const CApp&) = delete;
        CApp(CApp&)       = delete;
        CApp(CApp&&)      = delete;

        bool        appAlive() const;
        bool        operator==(const glz::generic& object) const;

        void        quit();
        void        kill();

        std::string m_address;
        std::string m_title;
        std::string m_class;
        int64_t     m_pid          = -1;
        bool        m_xwayland     = false;
        bool        m_alwaysUsePid = false;
    };

    class CAppState {
      public:
        CAppState()  = default;
        ~CAppState() = default;

        CAppState(const CAppState&) = delete;
        CAppState(CAppState&)       = delete;
        CAppState(CAppState&&)      = delete;

        bool                         init();
        bool                         updateState();
        float                        secondsPassed() const;
        void                         killAllApps() const;
        void                         reexitApps() const;

        const std::vector<UP<CApp>>& apps() const;

        bool                         m_dryRun = false;

      private:
        std::vector<UP<CApp>>                 m_apps;

        std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
    };

    SP<CAppState> state();
};