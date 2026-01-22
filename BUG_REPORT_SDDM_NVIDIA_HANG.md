# Bug Report: hyprshutdown Hangs on Logout with SDDM + NVIDIA

**Date:** 2026-01-22  
**Branch:** `investigate/sddm-nvidia-hang-fix`  
**Status:** ✅ RESOLVED

---

## Executive Summary

hyprshutdown (and Hyprland in general) appeared to "hang" during logout when using SDDM display manager with NVIDIA GPUs. After systematic investigation and testing, the **actual root cause** was identified:

**The display doesn't automatically switch back to SDDM's virtual terminal (VT) when Hyprland exits on NVIDIA systems.**

The fix is simple: explicitly switch VTs after Hyprland exits using the new `--vt` flag.

---

## Environment

### Failing Configuration
- **Display Manager:** SDDM
- **GPU:** NVIDIA (proprietary driver)
- **Result:** ❌ Black screen after logout (Ctrl+Alt+F2 restores SDDM)

### Working Configuration  
- **Display Manager:** greetd + tuigreet
- **GPU:** NVIDIA (same system)
- **Result:** ✅ Works (greetd uses TTY, no VT switching needed)

---

## Root Cause Analysis

### Initial Hypotheses (DISPROVEN)

During investigation, several potential causes were identified:

| Hypothesis | Status | Evidence |
|------------|--------|----------|
| Exit sequence ordering (EGL cleanup before Hyprland exit) | ❌ Disproven | Reordering didn't fix the issue |
| Session tracking (double-fork creating orphaned sessions) | ❌ Disproven | `--no-fork` alone didn't fix the issue |
| Socket file descriptor leaks | ❌ Unrelated | Fixed for code quality, but not the cause |
| Missing validation in quit() | ❌ Unrelated | Fixed for code quality, but not the cause |

### Actual Root Cause (CONFIRMED)

**Virtual Terminal (VT) switching failure on NVIDIA + SDDM**

When Hyprland exits:
1. Hyprland releases its VT (e.g., VT1)
2. SDDM's greeter should activate on its VT (e.g., VT2)
3. **On NVIDIA, this automatic VT switch doesn't happen**
4. The display shows a black screen (stuck on released VT)
5. Ctrl+Alt+F2 manually switches to SDDM's VT, restoring the greeter

**Proof:** `hyprctl dispatch exit` (vanilla Hyprland, no hyprshutdown) exhibits the **exact same hang behavior**.

---

## Test Results

### Systematic Testing

| Test Case | Command | Result |
|-----------|---------|--------|
| Vanilla Hyprland exit | `hyprctl dispatch exit` | ❌ HANGS |
| hyprshutdown default | `hyprshutdown` | ❌ HANGS |
| With --no-fork | `hyprshutdown --no-fork` | ❌ HANGS |
| With --vt 2 | `hyprshutdown --vt 2` | ✅ WORKS |
| With --vt auto | `hyprshutdown --vt auto` | ✅ WORKS |
| Combined | `hyprshutdown --no-fork --vt 2` | ✅ WORKS |

### Conclusions

1. **`--vt` is the ONLY required fix** for the NVIDIA+SDDM hang
2. **`--no-fork` is NOT required** - session tracking was not the issue
3. **Exit sequence reordering is NOT required** - helpful for code clarity but doesn't fix this issue
4. **This affects ALL Hyprland exits on NVIDIA+SDDM**, not just hyprshutdown

---

## The Fix

### Implementation

Added `--vt` command-line option that performs an async VT switch after Hyprland exits:

```cpp
// In UI.cpp exit sequence
if (vtSwitch) {
    int targetVT = (*vtSwitch == "auto") ? detectGreeterVT() : std::stoi(*vtSwitch);
    if (targetVT > 0) {
        switchToVTAsync(targetVT);  // Uses CProcess::runAsync()
    }
}
```

**Critical detail:** The VT switch MUST be async (non-blocking). Synchronous VT switching via ioctl causes hangs because `VT_WAITACTIVE` blocks indefinitely on NVIDIA during compositor shutdown.

### User Setup Required

The VT switch uses `sudo chvt`, which requires a sudoers rule:

```bash
echo "username ALL=(ALL) NOPASSWD: /usr/bin/chvt" | sudo tee /etc/sudoers.d/chvt
sudo chmod 440 /etc/sudoers.d/chvt
```

**Security note:** This is safe because `chvt` only switches virtual terminals and cannot be exploited for privilege escalation.

### Usage

```bash
# Explicit VT
hyprshutdown --vt 2

# Auto-detect (defaults to VT2 for SDDM)
hyprshutdown --vt auto
```

---

## Code Quality Improvements (Bonus)

While investigating, several code quality issues were identified and fixed. These are **NOT related to the SDDM hang** but improve the codebase:

### 1. Socket FD Leak (HyprlandIPC.cpp)

**Before:** Socket not closed on connect/write failure
**After:** `close(SERVERSOCKET)` added to error paths

### 2. Validation in CApp::quit() (AppState.cpp)

**Before:** Could proceed with empty address or invalid PID
**After:** Proper validation and early return with logging

### 3. Error Check Logic (AppState.cpp)

**Before:** `if (!ret) ... if (*ret != "ok")` - would dereference after error
**After:** `if (!ret) ... else if (*ret != "ok")` - proper else-if chain

### 4. Debug Logging (UI.cpp)

Added comprehensive debug logging throughout the exit sequence for future troubleshooting.

---

## Files Modified

| File | Changes | Purpose |
|------|---------|---------|
| `src/main.cpp` | Added `--vt` option | **THE FIX** |
| `src/ui/UI.cpp` | VT switch implementation, exit sequence improvements | **THE FIX** + code quality |
| `src/ui/UI.hpp` | Added `m_vtSwitch` member | **THE FIX** |
| `src/state/HyprlandIPC.cpp` | Socket FD leak fix | Code quality |
| `src/state/AppState.cpp` | Validation fixes | Code quality |
| `README.md` | Documentation for NVIDIA+SDDM users | Documentation |

---

## Why greetd Works

greetd + tuigreet works on the same NVIDIA system because:

1. **tuigreet runs in a TTY** - No GPU/Wayland involvement
2. **No VT switch needed** - Hyprland and greetd can share the same VT concept differently
3. **Simpler display stack** - TTY doesn't have the same VT switching requirements

---

## References

### Related Hyprland Issues
- [Issue #4399](https://github.com/hyprwm/Hyprland/issues/4399) - loginctl terminate-session crashes SDDM
- [Issue #8680](https://github.com/hyprwm/Hyprland/issues/8680) - Hyprland freezes with NVIDIA 565 + SDDM
- [Issue #8752](https://github.com/hyprwm/Hyprland/issues/8752) - System crashes to SDDM with NVIDIA

### Community Resources
- [Arch Forums: NVIDIA + SDDM problems](https://bbs.archlinux.org/viewtopic.php?id=295481)
- [Hyprland Wiki - NVIDIA](https://wiki.hyprland.org/Nvidia/)

---

## Recommendation for Upstream

This fix should be considered for upstream hyprshutdown with the following notes:

1. **The `--vt` flag is the minimal required change** for NVIDIA+SDDM users
2. **Auto-detection defaults to VT2** which is common for SDDM, but users can override
3. **Requires user setup** (sudoers rule for chvt) - this should be documented
4. **The `--no-fork` flag can be kept** for users who want foreground execution, but it's not required for this fix

---

*Investigation completed 2026-01-22*
