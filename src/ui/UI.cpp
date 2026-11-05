#include "UI.hpp"
#include "../helpers/Logger.hpp"
#include "../state/AppState.hpp"
#include "../state/HyprlandIPC.hpp"

#include <algorithm>

#include <hyprtoolkit/core/Output.hpp>
#include <hyprtoolkit/types/SizeType.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/os/Process.hpp>

using namespace Hyprutils::OS;

namespace {
    using ButtonPtr                   = Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>;
    constexpr float kButtonBaseHeight = 25.F;
    constexpr float kButtonFontScale  = 0.40F;

    template <typename OnClick, typename Configure = std::function<void(const ButtonPtr&)>>
    ButtonPtr makeButton(std::string_view label, OnClick&& onClick, float padding = 0.F, Configure configure = {}) {
        padding = std::max(0.F, padding);

        const float buttonHeight = kButtonBaseHeight + (padding * 2.F);
        const float fontSize     = std::max(10.F, buttonHeight * kButtonFontScale);

        auto        btn =
            Hyprtoolkit::CButtonBuilder::begin()
                ->label(std::string{label})
                ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_ABSOLUTE, fontSize})
                ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {0.F, buttonHeight}})
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

static Hyprtoolkit::CHyprColor parseColor(std::string str, const Hyprtoolkit::CHyprColor& fallback) {
    if (str.empty())
        return fallback;
    if (str.front() == '#')
        str = str.substr(1);
    if (str.starts_with("0x") || str.starts_with("0X"))
        str = str.substr(2);
    if (str.size() != 6 && str.size() != 8)
        return fallback;
    try {
        uint64_t val = std::stoul(str, nullptr, 16);
        if (str.size() == 6) {
            float r = ((val >> 16) & 0xff) / 255.f;
            float g = ((val >> 8) & 0xff) / 255.f;
            float b = (val & 0xff) / 255.f;
            return Hyprtoolkit::CHyprColor{r, g, b, 1.f};
        } else {
            float a = ((val >> 24) & 0xff) / 255.f;
            float r = ((val >> 16) & 0xff) / 255.f;
            float g = ((val >> 8) & 0xff) / 255.f;
            float b = (val & 0xff) / 255.f;
            return Hyprtoolkit::CHyprColor{r, g, b, a};
        }
    } catch (...) { return fallback; }
}

CMonitorState::SAppListApp::SAppListApp(const std::string_view& clazz, const std::string_view& title, bool quitSent, bool isProcess, bool isLayer) :
    m_rawClass(clazz),
    m_quitSent(quitSent),
    m_isProcess(isProcess),
    m_isLayer(isLayer) {

    m_null = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->commence();
    m_null->setMargin(State::state()->m_rowMargin);

    m_rowLayout =
        Hyprtoolkit::CRowLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(8)->commence();

    m_layout = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->commence();

    std::string titleColor = isProcess ? "#7f849c" : "#ffffff";
    std::string classText;
    if (isProcess) {
        classText = std::format("<span color='#7f849c'>⚙ {}</span>", clazz);
    } else if (isLayer) {
        classText = std::format("<span color='#ffffff'>🗗 {}</span>", clazz);
    } else {
        classText = std::format("<span color='#ffffff'>🗖 {}</span>", clazz);
    }

    m_class = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::move(classText))
                  ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                  ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H3})
                  ->commence();

    m_class->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_class->setAbsolutePosition({0.F, 0.F});
    m_layout->addChild(m_class);

    if (!title.empty()) {
        m_title = Hyprtoolkit::CTextBuilder::begin()
                      ->text(std::format("<span color='{}'><i>{}</i></span>", titleColor, title))
                      ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                      ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                      ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                      ->commence();

        m_title->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
        m_title->setAbsolutePosition({0.F, 22.F});
        m_layout->addChild(m_title);
    }

    m_lineNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    m_lineNull->setGrow(true, false);

    if (State::state()->m_lineWidth > 0) {
        m_line = Hyprtoolkit::CRectangleBuilder::begin()
                     ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, static_cast<float>(State::state()->m_lineWidth)}})
                     ->color([] { return parseColor(State::state()->m_lineColor, g_ui->backend()->getPalette()->m_colors.text); })
                     ->commence();
        m_line->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
        m_line->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
        m_line->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
        m_line->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
        m_lineNull->addChild(m_line);
    }

    std::string iconText;
    if (quitSent) {
        iconText = "<span color='#F9E2AF'>⠋</span>";
    } else {
        if (isProcess) {
            iconText = "<span color='#7f849c'>•</span>";
        } else {
            iconText = "<span color='#ffffff'>•</span>";
        }
    }

    m_icon = Hyprtoolkit::CTextBuilder::begin()
                 ->text(std::move(iconText))
                 ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                 ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H3})
                 ->commence();

    m_iconNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {24.F, 1.F}})->commence();
    m_icon->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_icon->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
    m_icon->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
    m_iconNull->addChild(m_icon);

    m_rowLayout->addChild(m_layout);
    m_rowLayout->addChild(m_lineNull);
    m_rowLayout->addChild(m_iconNull);

    m_null->addChild(m_rowLayout);
}

void CMonitorState::SAppListApp::updateText(bool quitSent, int frameIndex) {
    static const std::vector<std::string> SPINNER_FRAMES = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

    if (m_quitSent == quitSent && quitSent) {
        std::string newText = std::format("<span color='#F9E2AF'>{}</span>", SPINNER_FRAMES.at(frameIndex));
        m_icon->rebuild()->text(std::move(newText))->commence();
    } else if (m_quitSent != quitSent) {
        m_quitSent = quitSent;
        std::string newText;
        if (m_quitSent) {
            newText = std::format("<span color='#F9E2AF'>{}</span>", SPINNER_FRAMES.at(frameIndex));
        } else {
            if (m_isProcess) {
                newText = "<span color='#7f849c'>•</span>";
            } else {
                newText = "<span color='#ffffff'>•</span>";
            }
        }
        m_icon->rebuild()->text(std::move(newText))->commence();
    }
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
                    ->text("Waiting for your apps to exit.\n<i>You can force quit Hyprland, but that risks losing unsaved progress.</i>")
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

    m_forceLayer = makeButton("Force layer", [](auto) { State::state()->forceCurrentStage(); }, 8.F);

    m_buttonLayout->addChild(m_cancel);
    m_buttonLayout->addChild(m_forceLayer);
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

void CMonitorState::update() {
    m_apps.clear();
    m_appListLayout->clearChildren();

    const auto&           APPS = State::state()->apps();

    bool                  hasCurrentStage = false;
    State::SShutdownStage currentStage;
    if (State::state()->m_stageIndex < State::state()->m_stages.size()) {
        currentStage    = State::state()->m_stages.at(State::state()->m_stageIndex);
        hasCurrentStage = true;
    }

    State::SShutdownStage lastStage = {.category = static_cast<State::EAppCategory>(-1), .layer = -1};
    bool                  first     = true;

    for (const auto& APP : APPS) {
        if (APP->m_hidden || APP->m_hasWindow) {
            continue;
        }

        State::SShutdownStage appStage = {.category = APP->m_category, .layer = APP->m_layer};
        if (!first && (appStage.category != lastStage.category || appStage.layer != lastStage.layer)) {
            auto dividerNull =
                Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 24.F}})->commence();
            auto dividerLine = Hyprtoolkit::CRectangleBuilder::begin()
                                   ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 1.F}})
                                   ->color([] {
                                       auto col = parseColor(State::state()->m_lineColor, g_ui->backend()->getPalette()->m_colors.text);
                                       col.a *= 0.25F;
                                       return col;
                                   })
                                   ->commence();
            dividerLine->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
            dividerLine->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
            dividerLine->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
            dividerLine->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
            dividerNull->addChild(dividerLine);
            m_appListLayout->addChild(dividerNull);
        }

        lastStage = appStage;
        first     = false;

        bool inActiveStage = hasCurrentStage && State::state()->isAppInStage(*APP, currentStage) && APP->appAlive();
        m_apps.emplace_back(makeUnique<SAppListApp>(APP->m_class, APP->m_title, inActiveStage, APP->isProcess(), APP->isLayer()));
        m_appListLayout->addChild(m_apps.back()->m_null);
    }
}
void CMonitorState::tickSpinners(int frameIndex) {
    const auto& APPS            = State::state()->apps();
    size_t      visibleAppCount = 0;
    for (const auto& APP : APPS) {
        if (!APP->m_hidden && !APP->m_hasWindow) {
            visibleAppCount++;
        }
    }

    if (visibleAppCount != m_apps.size()) {
        update();
        return;
    }

    bool                  hasCurrentStage = false;
    State::SShutdownStage currentStage;
    if (State::state()->m_stageIndex < State::state()->m_stages.size()) {
        currentStage    = State::state()->m_stages.at(State::state()->m_stageIndex);
        hasCurrentStage = true;
    }

    size_t uiIndex = 0;
    for (size_t i = 0; i < APPS.size(); ++i) {
        if (APPS.at(i)->m_hidden || APPS.at(i)->m_hasWindow) {
            continue;
        }
        bool inActiveStage = hasCurrentStage && State::state()->isAppInStage(*APPS.at(i), currentStage) && APPS.at(i)->appAlive();
        m_apps.at(uiIndex)->updateText(inActiveStage, frameIndex);
        uiIndex++;
    }
}

void CUI::registerOutput(const SP<Hyprtoolkit::IOutput>& mon) {
    m_states.emplace_back(makeUnique<CMonitorState>(mon));
    mon->m_events.removed.listenStatic([this, m = WP<Hyprtoolkit::IOutput>{mon}] { std::erase_if(m_states, [&m](const auto& e) { return e->m_monitorName == m->port(); }); });
}

void CUI::exit(bool closeHl) {
    g_ui->m_states.clear();

    g_ui->backend()->addIdle([this, closeHl] {
        g_ui->m_backend->destroy();
        g_ui->m_backend.reset();

        if (closeHl && !m_noExit && !State::state()->m_dryRun) {
            //NOLINTNEXTLINE
            std::string cmd = State::state()->m_useLua ? "/dispatch hl.dsp.exit()" : "/dispatch exit";
            const auto RET = HyprlandIPC::getFromSocket(cmd);
            if (!RET) {
                g_logger->log(LOG_ERR, "Failed to exit Hyprland: {}", RET.error());
            }
            if (State::state()->m_systemdUserExit) {
                CProcess proc("/bin/sh", {"-c", "systemctl --user exit"});
                proc.runAsync();
            }
            if (m_postExitCmd) {
                CProcess proc("/bin/sh", {"-c", m_postExitCmd.value()});
                proc.runAsync();
            }
        }
    });
}

void CUI::setTimer() {
    // every 5 seconds or so, attempt to sigterm apps again
    static uint16_t          counter     = 0;
    constexpr const uint16_t COUNTER_MAX = 30;

    m_updateTimer = m_backend->addTimer(
        std::chrono::milliseconds(150),
        [this](ASP<Hyprtoolkit::CTimer> timer, void* d) {
            if (State::state()->apps().empty()) {
                exit(true);
                return;
            }

            counter++;

            if (counter > COUNTER_MAX) {
                g_logger->log(LOG_DEBUG, "Re-closing apps");
                counter = 0;
                State::state()->reexitApps();
            }

            m_spinnerFrame = (m_spinnerFrame + 1) % 10;
            State::state()->updateState();

            for (const auto& s : m_states) {
                s->tickSpinners(m_spinnerFrame);
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
