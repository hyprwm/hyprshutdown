# Bug Report: hyprshutdown Hangs on Logout with SDDM + NVIDIA

**Date:** 2026-01-22  
**Branch:** `investigate/sddm-nvidia-hang-fix`  
**Reporter:** Investigation conducted via code analysis  
**Status:** Under Investigation / Testing Required

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Environment Details](#environment-details)
3. [Problem Description](#problem-description)
4. [Codebase Architecture Overview](#codebase-architecture-overview)
5. [General Bugs Found](#general-bugs-found)
6. [SDDM+NVIDIA Specific Issues](#sddmnvidia-specific-issues)
7. [Root Cause Analysis](#root-cause-analysis)
8. [Implemented Fixes](#implemented-fixes)
9. [Files Modified](#files-modified)
10. [Testing Instructions](#testing-instructions)
11. [Additional Investigation Areas](#additional-investigation-areas)
12. [References](#references)

---

## Executive Summary

hyprshutdown works correctly on systems using greetd+tuigreet but hangs during logout when using SDDM display manager, particularly on systems with NVIDIA GPUs. This investigation identified several potential causes including:

- **Exit sequence ordering** that may cause NVIDIA driver GPU context cleanup to block
- **Double-fork daemon pattern** that escapes systemd-logind session tracking
- **Socket file descriptor leaks** in IPC communication
- **Missing validation** for edge cases in app termination

A fix branch has been created with proposed solutions for testing.

---

## Environment Details

### Working Configuration (Laptop)
- **Display Manager:** greetd + tuigreet
- **GPU:** Integrated (non-NVIDIA)
- **Greeter Theme:** N/A
- **Result:** ✅ Works flawlessly

### Failing Configuration (Desktop)
- **Display Manager:** SDDM
- **GPU:** NVIDIA (proprietary driver assumed)
- **Greeter Theme:** Catppuccin
- **Result:** ❌ Hangs on logout

---

## Problem Description

When running `hyprshutdown` on a system with:
- SDDM as the display manager
- NVIDIA GPU with proprietary drivers

The application hangs during the logout/shutdown sequence. The system does not complete the logout process and appears to freeze.

**Expected Behavior:** All applications close gracefully, Hyprland exits, and control returns to SDDM greeter.

**Actual Behavior:** The process hangs indefinitely during logout. Specific hang point unknown without runtime debugging.

---

## Codebase Architecture Overview

### Application Flow

```
main.cpp
    │
    ├── Parse arguments
    ├── Check HYPRLAND_INSTANCE_SIGNATURE
    ├── forkoff() ──────────────────────────────┐
    │       │                                    │
    │       ├── fork() + exit parent            │
    │       ├── setsid() ← Creates new session  │ ◄── POTENTIAL ISSUE #1
    │       ├── signal(SIGHUP, SIG_IGN)         │
    │       └── fork() + exit parent            │
    │                                           │
    ├── State::state()->init() ─────────────────┤
    │       │                                    │
    │       ├── Get clients via IPC             │
    │       ├── Get layers via IPC              │
    │       ├── Get child processes (PPid)      │
    │       └── Send quit() to all apps         │
    │                                           │
    └── g_ui->run() ────────────────────────────┤
            │                                    │
            ├── Create hyprtoolkit backend      │
            ├── Register outputs (monitors)     │
            ├── Create layer shell windows      │
            ├── Set 150ms timer for updates     │
            │                                    │
            └── On all apps closed:             │
                    │                            │
                    └── CUI::exit() ────────────┤
                            │                    │
                            ├── Clear UI states │
                            ├── Add idle callback│
                            │       │            │
                            │       ├── Destroy backend ◄── POTENTIAL ISSUE #2
                            │       └── IPC: /dispatch exit
                            │                    │
                            └── (process ends)  │
```

### Key Components

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, argument parsing, fork/daemonization |
| `src/ui/UI.cpp` | UI management, layer shell windows, exit sequence |
| `src/ui/UI.hpp` | UI class definitions |
| `src/state/AppState.cpp` | Application tracking, SIGTERM/closewindow logic |
| `src/state/AppState.hpp` | App state class definitions |
| `src/state/HyprlandIPC.cpp` | Hyprland socket communication |
| `src/state/HyprlandIPC.hpp` | IPC function declarations |
| `src/helpers/OS.cpp` | Process enumeration, PID/PPid queries |

---

## General Bugs Found

### Bug #1: Socket File Descriptor Leak

**Location:** `src/state/HyprlandIPC.cpp`, lines 68-74

**Description:** When `connect()` or `write()` fails, the socket file descriptor is never closed, causing a resource leak.

**Original Code:**
```cpp
if (connect(SERVERSOCKET, rc<sockaddr*>(&serverAddress), SUN_LEN(&serverAddress)) < 0)
    return std::unexpected(std::format("couldn't connect to the hyprland socket at {}", socketPath));

auto sizeWritten = write(SERVERSOCKET, cmd.c_str(), cmd.length());

if (sizeWritten < 0)
    return std::unexpected("couldn't write (4)");
```

**Impact:** File descriptor exhaustion over time; unlikely to cause immediate hang but is a correctness issue.

**Severity:** Low (general bug)

---

### Bug #2: Missing Validation in CApp::quit()

**Location:** `src/state/AppState.cpp`, lines 44-60

**Description:** The `quit()` method could attempt operations on apps with empty addresses or invalid PIDs without proper validation.

**Original Code:**
```cpp
void CApp::quit() {
    if (!m_alwaysUsePid && (!m_address.empty() || m_pid <= 0)) {
        // Could proceed with empty address
        auto ret = HyprlandIPC::getFromSocket(std::format("/dispatch closewindow address:{}", m_address));
        // ...
    } else {
        // Could proceed with invalid m_pid
        if (::kill(m_pid, SIGTERM) != 0)
            // ...
    }
}
```

**Impact:** Could send malformed IPC commands or signal operations on invalid PIDs.

**Severity:** Low-Medium (could cause unexpected behavior)

---

### Bug #3: Potential Dereference After Error Check

**Location:** `src/state/AppState.cpp`, lines 49-53

**Original Code:**
```cpp
auto ret = HyprlandIPC::getFromSocket(...);
if (!ret)
    g_logger->log(LOG_ERR, "Failed closing window {}: ipc err", m_class);

if (*ret != "ok")  // ← Dereferences ret even if previous check failed!
    g_logger->log(LOG_ERR, "Failed closing window {}: {}", m_class, *ret);
```

**Impact:** Potential undefined behavior if `ret` is in error state.

**Severity:** Medium (logic error)

---

## SDDM+NVIDIA Specific Issues

### Issue #1: Exit Sequence Order (HIGH PRIORITY)

**Location:** `src/ui/UI.cpp`, `CUI::exit()` method

**Description:** The original exit sequence was:
1. Clear UI states (RAII destroys windows)
2. Schedule idle callback
3. **Destroy hyprtoolkit backend** (releases EGL/GL contexts)
4. Send `/dispatch exit` to Hyprland

**Why This Causes NVIDIA Hangs:**

NVIDIA proprietary drivers maintain GPU context tied to the Wayland client connection. When the backend is destroyed:

1. EGL context teardown begins
2. NVIDIA driver attempts to sync pending GPU operations
3. **The compositor (Hyprland) is still running and may have pending frames**
4. Driver blocks waiting for GPU sync that depends on compositor
5. Result: **Deadlock**

With other drivers (Intel, AMD), the cleanup is more forgiving and doesn't block.

**Evidence:** This is a known pattern with NVIDIA on Wayland. See:
- Hyprland wiki NVIDIA page
- Multiple reports of EGL cleanup hangs

---

### Issue #2: Double-Fork Session Escape (MEDIUM PRIORITY)

**Location:** `src/main.cpp`, `forkoff()` function

**Description:** The double-fork daemon pattern includes `setsid()` which creates a new session:

```cpp
static void forkoff() {
    pid_t pid = fork();
    // ...
    if (setsid() < 0)  // ← Creates NEW session
        exit(EXIT_FAILURE);
    // ...
}
```

**Why This May Cause SDDM Hangs:**

1. SDDM uses systemd-logind for session management
2. User sessions are tracked via cgroups (e.g., `session-N.scope`)
3. `setsid()` creates a new session, potentially moving hyprshutdown **outside** the login session's cgroup
4. When SDDM/logind waits for session cleanup, it may not see hyprshutdown
5. But hyprshutdown is still holding Wayland/GPU resources
6. Result: **SDDM waits for session to end, but resources aren't released**

**Why greetd Works:**
greetd may have different session tracking behavior or be more lenient about process tree management.

---

### Issue #3: No Explicit Window Unmapping

**Location:** `src/ui/UI.cpp`

**Description:** Layer shell windows are destroyed implicitly via RAII when `m_states.clear()` is called. There's no explicit:
1. Unmap surface
2. Commit
3. Wait for server acknowledgment

NVIDIA drivers can hang if wl_surface destruction races with pending GPU operations.

---

### Issue #4: 5-Second IPC Timeout During Exit

**Location:** `src/state/HyprlandIPC.cpp`, line 55

```cpp
auto t = timeval{.tv_sec = 5, .tv_usec = 0};
setsockopt(SERVERSOCKET, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(struct timeval));
```

During the exit sequence, if Hyprland's socket is in a degraded state (common during shutdown), this 5-second timeout adds to perceived hang time.

---

## Root Cause Analysis

### Most Likely Cause: Exit Sequence Order

```
┌─────────────────────────────────────────────────────────────────┐
│                     ORIGINAL SEQUENCE                            │
├─────────────────────────────────────────────────────────────────┤
│  1. Clear UI states                                              │
│  2. Destroy backend (EGL cleanup)  ◄── NVIDIA BLOCKS HERE       │
│  3. Send /dispatch exit to Hyprland                              │
│                                                                  │
│  Problem: Hyprland is still running when we try to cleanup      │
│           NVIDIA driver waits for compositor which is waiting   │
│           for... nothing. Deadlock.                              │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     FIXED SEQUENCE                               │
├─────────────────────────────────────────────────────────────────┤
│  1. Explicitly close windows                                     │
│  2. Clear UI states                                              │
│  3. Send /dispatch exit to Hyprland  ◄── Compositor starts exit │
│  4. Destroy backend (EGL cleanup)    ◄── Compositor already     │
│                                          shutting down, no block │
└─────────────────────────────────────────────────────────────────┘
```

### Secondary Cause: Session Tracking

The `--no-fork` option was added to test whether session cgroup tracking is involved.

---

## Implemented Fixes

### Fix #1: Socket Leak (HyprlandIPC.cpp)

Added `close(SERVERSOCKET)` on error paths:

```cpp
if (connect(SERVERSOCKET, rc<sockaddr*>(&serverAddress), SUN_LEN(&serverAddress)) < 0) {
    close(SERVERSOCKET);  // ← ADDED
    return std::unexpected(...);
}

if (sizeWritten < 0) {
    close(SERVERSOCKET);  // ← ADDED
    return std::unexpected(...);
}
```

### Fix #2: Exit Sequence Reorder (UI.cpp)

Changed to send `/dispatch exit` **before** backend destruction:

```cpp
void CUI::exit(bool closeHl) {
    // Explicitly close windows first
    for (auto& state : m_states) {
        state->closeWindow();
    }
    g_ui->m_states.clear();

    g_ui->backend()->addIdle([this, closeHl] {
        if (closeHl && !m_noExit && !State::state()->m_dryRun) {
            // Tell Hyprland to exit FIRST
            HyprlandIPC::getFromSocket("/dispatch exit");
            // Then run post-exit command
            if (m_postExitCmd) { ... }
        }
        
        // THEN destroy backend (compositor already shutting down)
        g_ui->m_backend->destroy();
        g_ui->m_backend.reset();
    });
}
```

### Fix #3: Add --no-fork Option (main.cpp)

New command-line flag to skip daemonization:

```cpp
ASSERT(parser.registerBoolOption("no-fork", "", 
    "Do not fork/daemonize (may help with SDDM session tracking)"));

// Later:
if (!parser.getBool("no-fork").value_or(false)) {
    forkoff();
} else {
    signal(SIGHUP, SIG_IGN);  // Still survive terminal disconnect
}
```

### Fix #4: Validation in CApp::quit() (AppState.cpp)

Added proper validation and fixed the error check logic:

```cpp
void CApp::quit() {
    if (!m_alwaysUsePid && (!m_address.empty() || m_pid <= 0)) {
        if (m_address.empty()) {
            g_logger->log(LOG_WARN, "app {} has no address and no valid pid, skipping", m_class);
            return;  // ← ADDED: Don't proceed with empty address
        }
        auto ret = HyprlandIPC::getFromSocket(...);
        if (!ret)
            g_logger->log(LOG_ERR, "...: {}", ret.error());
        else if (*ret != "ok")  // ← FIXED: else if instead of separate if
            g_logger->log(LOG_ERR, "...");
    } else {
        if (m_pid <= 0) {
            g_logger->log(LOG_WARN, "app {} has invalid pid {}, skipping", m_class, m_pid);
            return;  // ← ADDED: Don't signal invalid PID
        }
        // ...
    }
}
```

### Fix #5: Added closeWindow() Method (UI.hpp/UI.cpp)

New public method for explicit window closure:

```cpp
// UI.hpp
void closeWindow();

// UI.cpp
void CMonitorState::closeWindow() {
    if (m_window) {
        g_logger->log(LOG_DEBUG, "Closing window for monitor {}", m_monitorName);
        m_window->close();
    }
}
```

### Fix #6: Comprehensive Debug Logging

Added LOG_DEBUG calls throughout exit sequence:

```cpp
g_logger->log(LOG_DEBUG, "CUI::exit called, closeHl={}", closeHl);
g_logger->log(LOG_DEBUG, "Closing window for monitor {}", m_monitorName);
g_logger->log(LOG_DEBUG, "Idle callback: preparing to exit");
g_logger->log(LOG_DEBUG, "Sending Hyprland exit dispatch");
g_logger->log(LOG_DEBUG, "Destroying backend");
g_logger->log(LOG_DEBUG, "Exit complete");
```

---

## Files Modified

| File | Changes |
|------|---------|
| `src/main.cpp` | Added `--no-fork` option with conditional fork logic |
| `src/state/AppState.cpp` | Added validation, fixed error handling, improved logging |
| `src/state/HyprlandIPC.cpp` | Fixed socket FD leak on error paths |
| `src/ui/UI.cpp` | Reordered exit sequence, added explicit window close, added logging |
| `src/ui/UI.hpp` | Added `closeWindow()` method declaration |

### Diff Statistics

```
 src/main.cpp              | 12 +++++++++++-
 src/state/AppState.cpp    | 15 ++++++++++----
 src/state/HyprlandIPC.cpp |  8 ++++++--
 src/ui/UI.cpp             | 50 +++++++++++++++++++++++++++++++++++++++++------
 src/ui/UI.hpp             |  1 +
 5 files changed, 73 insertions(+), 13 deletions(-)
```

---

## Testing Instructions

### Build the Fix Branch

```bash
# Clone or fetch the latest
git fetch origin
git checkout investigate/sddm-nvidia-hang-fix

# Build (Nix)
nix build

# Or build manually
mkdir build && cd build
cmake ..
make
```

### Test Scenarios

#### Test 1: Verbose Mode (Identify Hang Point)

```bash
hyprshutdown --verbose
```

Watch the output. If it hangs, note the last log message - this identifies where the hang occurs.

#### Test 2: No-Fork Mode (Test Session Tracking Theory)

```bash
hyprshutdown --verbose --no-fork
```

If this works but Test 1 doesn't, the issue is session/cgroup related.

#### Test 3: Dry Run (Verify UI Works)

```bash
hyprshutdown --verbose --dry-run
```

This shows the UI without actually exiting anything. Useful to verify basic functionality.

### Collect Debug Information

If still hanging, collect:

```bash
# Journal logs
journalctl -b | grep -iE '(hyprshutdown|nvidia|sddm|hyprland|egl|drm)' > ~/hyprshutdown_journal.log

# Hyprland log
cat ~/.local/share/hyprland/hyprland.log > ~/hyprland.log

# System info
nvidia-smi > ~/nvidia_info.txt
hyprctl version >> ~/nvidia_info.txt
```

---

## Additional Investigation Areas

If the implemented fixes don't resolve the issue, investigate:

### 1. hyprtoolkit Backend Destruction

The `m_backend->destroy()` call is opaque. May need to:
- Check if hyprtoolkit does `wl_display_roundtrip()` during destruction
- Verify EGL context cleanup order
- Check for NVIDIA-specific code paths

### 2. Wayland Protocol Compliance

Verify that layer shell surfaces are being properly:
1. Unmapped (set NULL buffer)
2. Committed
3. Destroyed only after server acknowledgment

### 3. systemd-logind Integration

Check if Hyprland properly registers with logind:
```bash
loginctl show-session $(loginctl | grep $(whoami) | awk '{print $1}')
```

### 4. NVIDIA-Specific Environment Variables

Test with:
```bash
__GL_YIELD="USLEEP" hyprshutdown --verbose
__GL_THREADED_OPTIMIZATIONS=0 hyprshutdown --verbose
```

### 5. DRM Explicit Sync

Check Hyprland NVIDIA configuration:
- Is `render:explicit_sync = true` set?
- Is the kernel/driver version compatible?

---

## References

- [Hyprland Wiki - NVIDIA](https://wiki.hyprland.org/Nvidia/)
- [NVIDIA Wayland Known Issues](https://github.com/NVIDIA/egl-wayland/issues)
- [systemd-logind Session Tracking](https://www.freedesktop.org/software/systemd/man/logind.conf.html)
- [Wayland Layer Shell Protocol](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)

---

## Appendix: Original vs Fixed Code Comparison

### CUI::exit() - Before

```cpp
void CUI::exit(bool closeHl) {
    g_ui->m_states.clear();

    g_ui->backend()->addIdle([this, closeHl] {
        g_ui->m_backend->destroy();
        g_ui->m_backend.reset();

        if (closeHl && !m_noExit && !State::state()->m_dryRun) {
            HyprlandIPC::getFromSocket("/dispatch exit");
            if (m_postExitCmd) {
                CProcess proc("/bin/sh", {"-c", m_postExitCmd.value()});
                proc.runAsync();
            }
        }
    });
}
```

### CUI::exit() - After

```cpp
void CUI::exit(bool closeHl) {
    g_logger->log(LOG_DEBUG, "CUI::exit called, closeHl={}", closeHl);

    for (auto& state : m_states) {
        state->closeWindow();
    }
    g_ui->m_states.clear();

    g_ui->backend()->addIdle([this, closeHl] {
        g_logger->log(LOG_DEBUG, "Idle callback: preparing to exit");

        if (closeHl && !m_noExit && !State::state()->m_dryRun) {
            g_logger->log(LOG_DEBUG, "Sending Hyprland exit dispatch");
            auto postCmd = m_postExitCmd;
            
            auto ret = HyprlandIPC::getFromSocket("/dispatch exit");
            if (!ret) {
                g_logger->log(LOG_WARN, "Failed to send exit dispatch: {}", ret.error());
            }

            if (postCmd) {
                g_logger->log(LOG_DEBUG, "Running post-exit command: {}", *postCmd);
                CProcess proc("/bin/sh", {"-c", postCmd.value()});
                proc.runAsync();
            }
        }

        g_logger->log(LOG_DEBUG, "Destroying backend");
        g_ui->m_backend->destroy();
        g_ui->m_backend.reset();

        g_logger->log(LOG_DEBUG, "Exit complete");
    });
}
```

---

*End of Bug Report*
