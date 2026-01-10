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
    using ButtonPtr                        = Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>;
    constexpr float kButtonBaseHeight      = 25.F;
    constexpr float kButtonFontScale       = 0.40F;
    constexpr float kButtonCharWidthFactor = 0.6F;
    constexpr float kAppListItemHeight     = 64.F;
    constexpr float kAppListPadding        = 4.F;
    constexpr float kAppListLeftPadding    = 24.F;

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

CMonitorState::SAppListApp::SAppListApp(const std::string_view& clazz, const std::string_view& title, bool alternate) {
    const bool hasTitle = !title.empty();

    m_null =
        Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, kAppListItemHeight}})->commence();

    m_bg = Hyprtoolkit::CRectangleBuilder::begin()
               ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})
               ->color([alternate] {
                   auto col = alternate ? g_ui->backend()->getPalette()->m_colors.alternateBase : g_ui->backend()->getPalette()->m_colors.base;
                   col.a *= alternate ? 0.28F : 0.20F;
                   return col;
               })
               ->rounding(0)
               ->commence();
    m_row = Hyprtoolkit::CRowLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->gap(0)->commence();
    m_leftPad =
        Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {kAppListLeftPadding, 1.F}})->commence();
    m_layout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->gap(4)->commence();

    if (hasTitle) {
        m_title = Hyprtoolkit::CTextBuilder::begin()
                      ->text(std::format("<i>{}</i>", title))
                      ->color([] {
                          auto col = g_ui->backend()->getPalette()->m_colors.text;
                          col.a *= 0.75F;
                          return col;
                      })
                      ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                      ->commence();
    }

    m_class = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::string{clazz})
                  ->color([] { return g_ui->backend()->getPalette()->m_colors.accent; })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H3})
                  ->commence();

    m_titleNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();
    m_classNull = Hyprtoolkit::CNullBuilder::begin()
                      ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, hasTitle ? Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO : Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                      ->commence();

    if (!hasTitle) {
        m_class->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
        m_class->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
        m_class->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
    }

    if (hasTitle)
        m_titleNull->addChild(m_title);
    m_classNull->addChild(m_class);

    m_layout->addChild(m_classNull);
    if (hasTitle)
        m_layout->addChild(m_titleNull);

    m_row->addChild(m_leftPad);
    m_row->addChild(m_layout);
    m_null->addChild(m_bg);
    m_null->addChild(m_row);
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
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->gap(0)->commence();

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

void CMonitorState::update() {
    m_apps.clear();
    m_appListLayout->clearChildren();

    const auto& APPS = State::state()->apps();

    auto        topPad =
        Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, kAppListPadding}})->commence();
    m_appListLayout->addChild(topPad);

    size_t index = 0;
    for (const auto& APP : APPS) {
        m_apps.emplace_back(makeUnique<SAppListApp>(APP->m_class, APP->m_title, (index % 2) == 1));
        m_appListLayout->addChild(m_apps.back()->m_null);
        ++index;
    }

    auto bottomPad =
        Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, kAppListPadding}})->commence();
    m_appListLayout->addChild(bottomPad);
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
            HyprlandIPC::getFromSocket("/dispatch exit");
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

            if (!State::state()->updateState()) {
                setTimer();
                return; // no changes
            }

            for (const auto& s : m_states) {
                s->update();
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
