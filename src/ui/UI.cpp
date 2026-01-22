#include "UI.hpp"
#include "../helpers/Logger.hpp"
#include "../state/AppState.hpp"
#include "../state/HyprlandIPC.hpp"

#include <algorithm>
#include <thread>
#include <chrono>

#include <hyprtoolkit/core/Output.hpp>
#include <hyprtoolkit/types/SizeType.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/os/Process.hpp>

using namespace Hyprutils::OS;

namespace {
    using ButtonPtr                        = Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>;
    constexpr float kButtonBaseHeight      = 25.F;
    constexpr float kButtonFontScale       = 0.40F;
    constexpr float kButtonCharWidthFactor = 0.6F;

    float           buttonWidthForLabel(std::string_view label, float padding, float fontSize) {
        const float textWidth = static_cast<float>(label.size()) * (fontSize * kButtonCharWidthFactor);
        return textWidth + (std::max(0.F, padding) * 2.F);
    }

    template <typename OnClick, typename Configure = std::function<void(const ButtonPtr&)>>
    ButtonPtr makeButton(std::string_view label, OnClick&& onClick, float padding = 0.F, Configure configure = {}) {
        padding = std::max(0.F, padding);

        const float buttonHeight = kButtonBaseHeight + (padding * 2.F);
        const float fontSize     = std::max(10.F, buttonHeight * kButtonFontScale);

        auto        btn =
            Hyprtoolkit::CButtonBuilder::begin()
                ->label(std::string{label})
                ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_ABSOLUTE, fontSize})
                ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {buttonWidthForLabel(label, padding, fontSize), buttonHeight}})
                ->onMainClick(std::forward<OnClick>(onClick))
                ->commence();

        if (configure) {
            configure(btn);
        }

        return btn;
    }
}

CUI::CUI()  = default;
CUI::~CUI() = default;

CMonitorState::SAppListApp::SAppListApp(const std::string_view& clazz, const std::string_view& title, int64_t pid, bool isXwayland) {
    // Outer container with margin
    m_null = Hyprtoolkit::CNullBuilder::begin()
                 ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                 ->commence();
    m_null->setMargin(4);

    // Card background with subtle color and rounding
    m_cardBg = Hyprtoolkit::CRectangleBuilder::begin()
                   ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                   ->color([] {
                       auto col = g_ui->backend()->getPalette()->m_colors.surface;
                       col.a *= 0.6F;
                       return col;
                   })
                   ->rounding(g_ui->backend()->getPalette()->m_vars.smallRounding)
                   ->commence();

    // Content container with padding
    m_contentNull = Hyprtoolkit::CNullBuilder::begin()
                        ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                        ->commence();
    m_contentNull->setMargin(12);

    // Horizontal layout for status indicator + text
    m_rowLayout = Hyprtoolkit::CRowLayoutBuilder::begin()
                      ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                      ->gap(12)
                      ->commence();

    // Status indicator (closing indicator)
    std::string statusIcon = "●";  // Closing indicator
    std::string statusColor = isXwayland ? "#f9e2af" : "#a6e3a1";  // Yellow for XWayland, green for native
    m_status = Hyprtoolkit::CTextBuilder::begin()
                   ->text(std::format("<span foreground=\"{}\">●</span>", statusColor))
                   ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                   ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H2})
                   ->commence();

    // Vertical layout for class + title
    m_textLayout = Hyprtoolkit::CColumnLayoutBuilder::begin()
                       ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                       ->gap(2)
                       ->commence();

    // App class name (bold/larger)
    m_class = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::string{clazz})
                  ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H3})
                  ->commence();

    // App title with PID info (smaller, italicized)
    std::string titleText = title.empty() ? "" : std::string{title};
    std::string pidInfo = pid > 0 ? std::format(" <span alpha=\"60%\">(PID: {})</span>", pid) : "";
    std::string xwaylandTag = isXwayland ? " <span foreground=\"#f9e2af\">[X11]</span>" : "";
    m_title = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::format("<i>{}</i>{}{}", titleText, pidInfo, xwaylandTag))
                  ->color([] {
                      auto col = g_ui->backend()->getPalette()->m_colors.text;
                      col.a *= 0.8F;
                      return col;
                  })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                  ->commence();

    // Build the hierarchy
    m_textLayout->addChild(m_class);
    m_textLayout->addChild(m_title);

    m_rowLayout->addChild(m_status);
    m_rowLayout->addChild(m_textLayout);

    m_contentNull->addChild(m_rowLayout);
    m_cardBg->addChild(m_contentNull);
    m_null->addChild(m_cardBg);
}

CMonitorState::CMonitorState(SP<Hyprtoolkit::IOutput> output) : m_monitorName(output->port()) {
    m_window = Hyprtoolkit::CWindowBuilder::begin()
                   ->type(Hyprtoolkit::HT_WINDOW_LAYER)
                   ->prefferedOutput(output)
                   ->anchor(0xF)
                   ->layer(3)
                   ->preferredSize({0, 0})
                   ->exclusiveZone(-1)
                   ->appClass("hyprshutdown")
                   ->commence();

    m_bg = Hyprtoolkit::CRectangleBuilder::begin()
               ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
               ->color([] {
                   auto col = g_ui->backend()->getPalette()->m_colors.background;
                   col.a *= 0.9F;
                   return col;
               })
               ->commence();

    m_null = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {0.5F, 0.8F}})->commence();
    m_null->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_null->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_CENTER, true);

    m_layout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})->gap(5)->commence();

    m_topText = Hyprtoolkit::CTextBuilder::begin()
                    ->text(std::string{g_ui->m_shutdownLabel})
                    ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                    ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H1})
                    ->commence();

    m_subText = Hyprtoolkit::CTextBuilder::begin()
                    ->text("Waiting for apps to close <span alpha=\"60%\">(0s elapsed)</span>\n"
                           "<i>You can force quit Hyprland, but that risks losing unsaved progress.</i>")
                    ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                    ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                    ->commence();

    m_spacer  = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 20.F}})->commence();
    m_spacer2 = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 20.F}})->commence();

    m_appListNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 1.F}})->commence();
    m_appListNull->setGrow(true);

    m_appListRect = Hyprtoolkit::CRectangleBuilder::begin()
                        ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                        ->color([] { return Hyprtoolkit::CHyprColor{0}; })
                        ->rounding(g_ui->backend()->getPalette()->m_vars.bigRounding)
                        ->borderThickness(1)
                        ->borderColor([] { return g_ui->backend()->getPalette()->m_colors.accent; })
                        ->commence();

    m_appListScroll = Hyprtoolkit::CScrollAreaBuilder::begin()
                          ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                          ->scrollY(true)
                          ->scrollX(false)
                          ->commence();

    m_appListLayout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->gap(8)->commence();

    m_buttonLayout =
        Hyprtoolkit::CRowLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->gap(5)->commence();
    auto spacer3 = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 1.F}})->commence();
    spacer3->setGrow(true, false);

    m_buttonLayout->addChild(spacer3);

    m_forceQuit = makeButton(
        "Force quit",
        [](auto) {
            State::state()->killAllApps();
            g_ui->exit(true);
        },
        8.F);

    m_cancel = makeButton("Cancel", [](auto) { g_ui->exit(false); }, 8.F);

    m_buttonLayout->addChild(m_cancel);
    m_buttonLayout->addChild(m_forceQuit);

    m_window->m_rootElement->addChild(m_bg);
    m_window->m_rootElement->addChild(m_null);

    m_null->addChild(m_layout);

    m_appListNull->addChild(m_appListRect);
    m_appListRect->addChild(m_appListScroll);
    m_appListScroll->addChild(m_appListLayout);

    m_layout->addChild(m_topText);
    m_layout->addChild(m_subText);
    m_layout->addChild(m_spacer);
    m_layout->addChild(m_appListNull);
    m_layout->addChild(m_spacer2);
    m_layout->addChild(m_buttonLayout);

    update();

    m_window->open();
}

void CMonitorState::closeWindow() {
    if (m_window) {
        g_logger->log(LOG_DEBUG, "Closing window for monitor {}", m_monitorName);
        m_window->close();
    }
}

void CMonitorState::update() {
    m_apps.clear();
    m_appListLayout->clearChildren();

    const auto& APPS = State::state()->apps();
    const auto  elapsed = static_cast<int>(State::state()->secondsPassed());

    // Update subtitle with app count and elapsed time
    if (APPS.empty()) {
        m_subText->setText("All apps closed. Exiting...");
    } else {
        std::string appWord = APPS.size() == 1 ? "app" : "apps";
        m_subText->setText(std::format(
            "Waiting for <b>{}</b> {} to close <span alpha=\"60%\">({}s elapsed)</span>\n"
            "<i>You can force quit Hyprland, but that risks losing unsaved progress.</i>",
            APPS.size(), appWord, elapsed));
    }

    for (const auto& APP : APPS) {
        m_apps.emplace_back(makeUnique<SAppListApp>(APP->m_class, APP->m_title, APP->m_pid, APP->m_xwayland));
        m_appListLayout->addChild(m_apps.back()->m_null);
    }
}

void CUI::registerOutput(const SP<Hyprtoolkit::IOutput>& mon) {
    m_states.emplace_back(makeUnique<CMonitorState>(mon));
    mon->m_events.removed.listenStatic([this, m = WP<Hyprtoolkit::IOutput>{mon}] { std::erase_if(m_states, [&m](const auto& e) { return e->m_monitorName == m->port(); }); });
}

void CUI::exit(bool closeHl) {
    g_logger->log(LOG_DEBUG, "CUI::exit called, closeHl={}", closeHl);

    // First, explicitly close all windows to ensure layer surfaces are properly unmapped
    // This helps prevent NVIDIA driver hangs by ensuring surfaces are gone before backend destruction
    for (auto& state : m_states) {
        state->closeWindow();
    }
    g_ui->m_states.clear();

    g_ui->backend()->addIdle([this, closeHl] {
        g_logger->log(LOG_DEBUG, "Idle callback: preparing to exit");

        // For SDDM+NVIDIA compatibility: Send the Hyprland exit command BEFORE
        // destroying our backend. This prevents potential GPU context issues where
        // NVIDIA drivers block during EGL cleanup while Hyprland is still running.
        if (closeHl && !m_noExit && !State::state()->m_dryRun) {
            g_logger->log(LOG_DEBUG, "Sending Hyprland exit dispatch");

            // Capture post-exit command before any cleanup
            auto postCmd = m_postExitCmd;

            // Tell Hyprland to exit first - this will trigger compositor shutdown
            // which is the natural cleanup order for Wayland clients
            auto ret = HyprlandIPC::getFromSocket("/dispatch exit");
            if (!ret) {
                g_logger->log(LOG_WARN, "Failed to send exit dispatch: {}", ret.error());
            }

            // Run post-exit command if specified
            if (postCmd) {
                g_logger->log(LOG_DEBUG, "Running post-exit command: {}", *postCmd);
                CProcess proc("/bin/sh", {"-c", postCmd.value()});
                proc.runAsync();
            }
        }

        // Now destroy our backend - Hyprland should already be shutting down
        // so this cleanup should proceed without GPU contention
        g_logger->log(LOG_DEBUG, "Destroying backend");
        g_ui->m_backend->destroy();
        g_ui->m_backend.reset();

        g_logger->log(LOG_DEBUG, "Exit complete");
    });
}

void CUI::setTimer() {
    // every 5 seconds or so, attempt to sigterm apps again
    static uint16_t          counter     = 0;
    static uint16_t          uiCounter   = 0;
    constexpr const uint16_t COUNTER_MAX = 30;  // ~4.5s at 150ms intervals
    constexpr const uint16_t UI_UPDATE_INTERVAL = 7;  // ~1s at 150ms intervals

    m_updateTimer = m_backend->addTimer(
        std::chrono::milliseconds(150),
        [this](ASP<Hyprtoolkit::CTimer> timer, void* d) {
            if (State::state()->apps().empty()) {
                exit(true);
                return;
            }

            counter++;
            uiCounter++;

            if (counter > COUNTER_MAX) {
                g_logger->log(LOG_DEBUG, "Re-closing apps");
                counter = 0;
                State::state()->reexitApps();
            }

            bool stateChanged = State::state()->updateState();
            bool shouldUpdateUI = stateChanged || (uiCounter >= UI_UPDATE_INTERVAL);

            if (shouldUpdateUI) {
                uiCounter = 0;
                for (const auto& s : m_states) {
                    s->update();
                }
            }

            setTimer();
        },
        nullptr);
}

bool CUI::run() {
    auto data           = Hyprtoolkit::IBackend::SBackendCreationData();
    data.pLogConnection = makeShared<Hyprutils::CLI::CLoggerConnection>(*g_logger);
    data.pLogConnection->setName("hyprtoolkit");
    data.pLogConnection->setLogLevel(LOG_DEBUG);
    m_backend = Hyprtoolkit::IBackend::createWithData(data);

    if (!m_backend)
        return false;

    {
        const auto MONITORS = m_backend->getOutputs();

        for (const auto& m : MONITORS) {
            registerOutput(m);
        }

        m_listeners.newMon = m_backend->m_events.outputAdded.listen([this](SP<Hyprtoolkit::IOutput> mon) { registerOutput(mon); });

        g_logger->log(LOG_DEBUG, "Found {} output(s)", MONITORS.size());

        setTimer();
    }

    m_backend->enterLoop();

    return true;
}

SP<Hyprtoolkit::IBackend> CUI::backend() {
    return m_backend;
}
