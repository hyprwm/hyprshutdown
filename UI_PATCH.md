# UI Enhancement Patch Documentation

**Date:** 2026-01-22  
**Branch:** `investigate/sddm-nvidia-hang-fix`  
**Related:** [BUG_REPORT_SDDM_NVIDIA_HANG.md](./BUG_REPORT_SDDM_NVIDIA_HANG.md)

---

## Overview

This patch improves the hyprshutdown UI to better align with the quality and polish expected from the Hyprland ecosystem. The original UI was functional but basic, lacking visual feedback and modern styling that users expect.

---

## Files Modified

| File | Lines Changed |
|------|---------------|
| `src/ui/UI.hpp` | +5, -4 |
| `src/ui/UI.cpp` | +96, -36 |

---

## Changes Summary

### 1. App Card Styling

**Why:** The original display was a plain text list with no visual hierarchy. Apps were hard to distinguish and the UI felt unfinished compared to other Hyprland tools like hyprlock/hypridle.

**Original Code (`src/ui/UI.hpp:51-58`):**
```cpp
struct SAppListApp {
    SAppListApp(const std::string_view& clazz, const std::string_view& title);

    SP<Hyprtoolkit::CNullElement>         m_null, m_titleNull, m_classNull;
    SP<Hyprtoolkit::CColumnLayoutElement> m_layout;
    SP<Hyprtoolkit::CTextElement>         m_title;
    SP<Hyprtoolkit::CTextElement>         m_class;
};
```

**New Code (`src/ui/UI.hpp:51-61`):**
```cpp
struct SAppListApp {
    SAppListApp(const std::string_view& clazz, const std::string_view& title, int64_t pid, bool isXwayland);

    SP<Hyprtoolkit::CNullElement>         m_null, m_contentNull;
    SP<Hyprtoolkit::CRectangleElement>    m_cardBg;
    SP<Hyprtoolkit::CRowLayoutElement>    m_rowLayout;
    SP<Hyprtoolkit::CColumnLayoutElement> m_textLayout;
    SP<Hyprtoolkit::CTextElement>         m_title;
    SP<Hyprtoolkit::CTextElement>         m_class;
    SP<Hyprtoolkit::CTextElement>         m_status;
};
```

**What Changed:**
- Constructor now takes `pid` and `isXwayland` parameters for richer display
- Added `m_cardBg` (CRectangleElement) for card background styling
- Added `m_rowLayout` for horizontal status indicator + text arrangement
- Added `m_status` for the colored status dot indicator
- Renamed `m_layout` to `m_textLayout` for clarity
- Removed separate `m_titleNull` and `m_classNull` (simplified hierarchy)

---

### 2. App Card Implementation

**Why:** Each app needed a visual "card" appearance with:
- Rounded corners matching Hyprland's aesthetic
- Subtle background color from the palette
- Status indicator showing app type (native vs XWayland)
- PID display for debugging shutdown issues

**Original Code (`src/ui/UI.cpp:54-90`):**
```cpp
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
```

**New Code (`src/ui/UI.cpp:54-131`):**
```cpp
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
```

**What Changed:**
- Added rounded card background using `CRectangleBuilder` with `smallRounding`
- Card uses `surface` color from palette at 60% alpha for subtlety
- Added colored status indicator dot:
  - 🟢 Green (`#a6e3a1`) for native Wayland apps
  - 🟡 Yellow (`#f9e2af`) for XWayland apps
- Title now includes PID at 60% alpha for debugging
- XWayland apps show `[X11]` tag in yellow
- Content has 12px internal padding for breathing room
- Cleaner element hierarchy without absolute positioning hacks

---

### 3. Dynamic Status Text

**Why:** The original static text provided no feedback about progress. Users couldn't tell how many apps remained or how long the process had been running.

**Original Code (`src/ui/UI.cpp:125-129`):**
```cpp
m_subText = Hyprtoolkit::CTextBuilder::begin()
                ->text("Waiting for your apps to exit.\n<i>You can force quit Hyprland, but that risks losing unsaved progress.</i>")
                ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                ->commence();
```

**New Code (`src/ui/UI.cpp:163-169`):**
```cpp
m_subText = Hyprtoolkit::CTextBuilder::begin()
                ->text("Waiting for apps to close <span alpha=\"60%\">(0s elapsed)</span>\n"
                       "<i>You can force quit Hyprland, but that risks losing unsaved progress.</i>")
                ->color([] { return g_ui->backend()->getPalette()->m_colors.text; })
                ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT})
                ->commence();
```

**What Changed:**
- Initial text now shows elapsed time placeholder
- Prepared for dynamic updates

---

### 4. Dynamic Update in `CMonitorState::update()`

**Why:** The status text needs to update dynamically to show:
- How many apps are still closing
- How long the user has been waiting
- A final "Exiting..." message when complete

**Original Code (`src/ui/UI.cpp:202-212`):**
```cpp
void CMonitorState::update() {
    m_apps.clear();
    m_appListLayout->clearChildren();

    const auto& APPS = State::state()->apps();

    for (const auto& APP : APPS) {
        m_apps.emplace_back(makeUnique<SAppListApp>(APP->m_class, APP->m_title));
        m_appListLayout->addChild(m_apps.back()->m_null);
    }
}
```

**New Code (`src/ui/UI.cpp:245-268`):**
```cpp
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
```

**What Changed:**
- Retrieves `secondsPassed()` from AppState for elapsed time
- Updates `m_subText` dynamically with app count and elapsed time
- Uses proper singular/plural ("1 app" vs "3 apps")
- Shows "All apps closed. Exiting..." when done
- App creation now passes `pid` and `xwayland` status

---

### 5. Timer-Based UI Updates

**Why:** Previously, the UI only updated when app state changed. This meant the elapsed time counter never ticked, appearing frozen. Users need visual feedback that the system is still working.

**Original Code (`src/ui/UI.cpp:266-299`):**
```cpp
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
```

**New Code (`src/ui/UI.cpp:319-358`):**
```cpp
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
```

**What Changed:**
- Added `uiCounter` to track time since last UI update
- Added `UI_UPDATE_INTERVAL = 7` (~1 second at 150ms timer)
- UI now updates either when:
  - App state changes (apps close), OR
  - ~1 second has passed (to update elapsed time display)
- Better comments explaining the timing constants

---

## Visual Comparison

### Before
```
┌─────────────────────────────────────────────────────────────┐
│                     Shutting down...                         │
│  Waiting for your apps to exit.                              │
│  You can force quit Hyprland, but that risks losing...       │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ kitty                                                │    │
│  │ ~/WMS/hyprland                                       │    │
│  │                                                      │    │
│  │ kitty                                                │    │
│  │ vaxry@Arch:~/WMS/hyprland                            │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│                              [Cancel] [Force quit]           │
└─────────────────────────────────────────────────────────────┘
```

### After
```
┌─────────────────────────────────────────────────────────────┐
│                     Shutting down...                         │
│  Waiting for 3 apps to close (12s elapsed)                   │
│  You can force quit Hyprland, but that risks losing...       │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ ┌─────────────────────────────────────────────────┐ │    │
│  │ │ 🟢  kitty                                        │ │    │
│  │ │     ~/WMS/hyprland (PID: 12345)                  │ │    │
│  │ └─────────────────────────────────────────────────┘ │    │
│  │ ┌─────────────────────────────────────────────────┐ │    │
│  │ │ 🟡  firefox                                      │ │    │
│  │ │     Mozilla Firefox (PID: 12346) [X11]           │ │    │
│  │ └─────────────────────────────────────────────────┘ │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│                              [Cancel] [Force quit]           │
└─────────────────────────────────────────────────────────────┘
```

---

## Color Reference

| Element | Color | Hex | Usage |
|---------|-------|-----|-------|
| Native Wayland indicator | Green | `#a6e3a1` | Status dot for native apps |
| XWayland indicator | Yellow | `#f9e2af` | Status dot and [X11] tag |
| Card background | Palette surface | 60% alpha | Subtle card backdrop |
| PID text | Palette text | 60% alpha | De-emphasized info |
| Title text | Palette text | 80% alpha | Secondary info |

*Colors chosen from Catppuccin palette for consistency with common Hyprland themes.*

---

## Future Enhancement Ideas

These are documented in [BUG_REPORT_SDDM_NVIDIA_HANG.md](./BUG_REPORT_SDDM_NVIDIA_HANG.md#ui-enhancement-proposal) for maintainer consideration:

1. Per-app "Force Close" buttons
2. Animated status indicators (pulse effect for waiting apps)
3. App icons from desktop files
4. Red indicator for hung/unresponsive apps
5. Progress bar for SIGTERM retry countdown

---

*End of UI Patch Documentation*
