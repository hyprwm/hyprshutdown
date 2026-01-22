## hyprshutdown
A graceful shutdown/logout utility for Hyprland, which prevents apps from crashing / dying unexpectedly.

![](./assets/preview.png)

## Usage

Just run `hyprshutdown`. This will close all apps and exit Hyprland.

See `hyprshutdown -h` for more information.

### NVIDIA + SDDM Users

If you experience a black screen / hang when logging out with NVIDIA GPU and SDDM display manager, use the `--vt` flag:

```bash
hyprshutdown --vt 2
```

**Why this is needed:** On NVIDIA systems with SDDM, the display doesn't automatically switch back to SDDM's virtual terminal (typically VT2) when Hyprland exits. The `--vt` flag forces a VT switch after logout.

**Setup:** The VT switch requires passwordless sudo for `chvt`:

```bash
echo "$USER ALL=(ALL) NOPASSWD: /usr/bin/chvt" | sudo tee /etc/sudoers.d/chvt
sudo chmod 440 /etc/sudoers.d/chvt
```

This is safe because `chvt` only switches virtual terminals and cannot be exploited for privilege escalation.

### Notes

`hyprshutdown` does **not** shut down the system, it only shuts down Hyprland.

`hyprshutdown` does not work with anything other than Hyprland, as it relies on Hyprland IPC.

### Command-Line Options

| Option | Description |
|--------|-------------|
| `--vt N` | Switch to VT N after exit (fixes NVIDIA+SDDM black screen) |
| `--dry-run` | Show UI without actually closing apps or exiting |
| `--no-exit` | Close apps but don't exit Hyprland |
| `--top-label`, `-t` | Custom text for the shutdown dialog |
| `--post-cmd`, `-p` | Command to run after Hyprland exits |
| `--no-fork` | Run in foreground (don't daemonize) |
| `--verbose` | Enable debug logging |
| `--help`, `-h` | Show help |
