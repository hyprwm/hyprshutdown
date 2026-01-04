#include "UI.hpp"
#include "../helpers/Logger.hpp"
#include "../state/AppState.hpp"
#include "../state/HyprlandIPC.hpp"

#include <hyprtoolkit/core/Output.hpp>

#include <hyprutils/os/Process.hpp>
using namespace Hyprutils::OS;

CUI::CUI()  = default;
CUI::~CUI() = default;

CMonitorState::SAppListApp::SAppListApp(const std::string_view& clazz, const std::string_view& title) {
    m_null = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->commence();
    m_null->setMargin(4);
    m_layout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})->gap(2)->commence();

    m_title = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::format("<i>{}</i>", title))
                  ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                  ->commence();

    m_class = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::string{clazz})
                  ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                  ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_H3})
                  ->commence();

    m_titleNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();
    m_classNull = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->commence();

    m_class->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_class->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
    m_class->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);

    m_title->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_title->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
    m_title->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);

    m_titleNull->addChild(m_title);
    m_classNull->addChild(m_class);

    m_layout->addChild(m_classNull);
    m_layout->addChild(m_titleNull);

    m_null->addChild(m_layout);
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
        Hyprtoolkit::CRowLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1, 25}})->gap(5)->commence();
    auto spacer3 = Hyprtoolkit::CNullBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 1.F}})->commence();
    spacer3->setGrow(true, false);

    m_buttonLayout->addChild(spacer3);

    m_forceQuit = Hyprtoolkit::CButtonBuilder::begin()
                      ->label("Force quit")
                      ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 25.F}})
                      ->onMainClick([](Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement> e) {
                          State::state()->killAllApps();
                          g_ui->exit(true);
                      })
                      ->commence();

    m_cancel = Hyprtoolkit::CButtonBuilder::begin()
                   ->label("Cancel")
                   ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 25.F}})
                   ->onMainClick([](Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement> e) { g_ui->exit(); })
                   ->commence();

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

    for (const auto& APP : APPS) {
        m_apps.emplace_back(makeUnique<SAppListApp>(APP->m_class, APP->m_title));
        m_appListLayout->addChild(m_apps.back()->m_null);
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

        if (closeHl) {
            if (m_postExitCmd) {
                CProcess proc("/bin/sh", {"-c", m_postExitCmd.value()});
                proc.runAsync();
            }

            if (!m_noExit) {
                //NOLINTNEXTLINE
                HyprlandIPC::getFromSocket("/dispatch exit");
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
