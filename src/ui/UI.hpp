#pragma once

#include <vector>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>

#include <hyprutils/signal/Listener.hpp>

#include "../helpers/Memory.hpp"

class CMonitorState {
  public:
    CMonitorState(SP<Hyprtoolkit::IOutput> output);
    ~CMonitorState() = default;

    CMonitorState(const CMonitorState&) = delete;
    CMonitorState(CMonitorState&)       = delete;
    CMonitorState(CMonitorState&&)      = delete;

    void        update();
    void        tickSpinners(int frameIndex);

    std::string m_monitorName;

  private:
    SP<Hyprtoolkit::IWindow>              m_window;

    SP<Hyprtoolkit::CRectangleElement>    m_bg;
    SP<Hyprtoolkit::CNullElement>         m_null;
    SP<Hyprtoolkit::CNullElement>         m_spacer, m_spacer2;
    SP<Hyprtoolkit::CColumnLayoutElement> m_layout;
    SP<Hyprtoolkit::CTextElement>         m_topText;
    SP<Hyprtoolkit::CTextElement>         m_subText;
    SP<Hyprtoolkit::CRowLayoutElement>    m_buttonLayout;
    SP<Hyprtoolkit::CButtonElement>       m_forceQuit, m_cancel, m_forceLayer;

    SP<Hyprtoolkit::CNullElement>         m_appListNull;
    SP<Hyprtoolkit::CRectangleElement>    m_appListRect;
    SP<Hyprtoolkit::CScrollAreaElement>   m_appListScroll;
    SP<Hyprtoolkit::CColumnLayoutElement> m_appListLayout;
    struct SAppListApp {
        SAppListApp(const std::string_view& clazz, const std::string_view& title, bool quitSent, bool isProcess, bool isLayer);
        void                               updateText(bool quitSent, int frameIndex);

        std::string                        m_rawClass;
        bool                               m_quitSent;
        bool                               m_isProcess = false;
        bool                               m_isLayer   = false;

        SP<Hyprtoolkit::CNullElement>      m_null, m_iconNull, m_lineNull, m_layout;
        SP<Hyprtoolkit::CRowLayoutElement> m_rowLayout;
        SP<Hyprtoolkit::CTextElement>      m_title;
        SP<Hyprtoolkit::CTextElement>      m_class;
        SP<Hyprtoolkit::CRectangleElement> m_line;
        SP<Hyprtoolkit::CTextElement>      m_icon;
    };

    std::vector<UP<SAppListApp>> m_apps;
};

class CUI {
  public:
    CUI();
    ~CUI();

    bool                       run();
    SP<Hyprtoolkit::IBackend>  backend();

    bool                       m_noExit = false;
    std::optional<std::string> m_postExitCmd;
    std::string                m_shutdownLabel;

  private:
    void                           registerOutput(const SP<Hyprtoolkit::IOutput>& mon);
    void                           setTimer();

    void                           exit(bool closeHl = false);

    SP<Hyprtoolkit::IBackend>      m_backend;
    ASP<Hyprtoolkit::CTimer>       m_updateTimer;
    int                            m_spinnerFrame = 0;

    std::vector<UP<CMonitorState>> m_states;

    struct {
        Hyprutils::Signal::CHyprSignalListener newMon;
    } m_listeners;

    friend class CMonitorState;
};

inline UP<CUI> g_ui;
