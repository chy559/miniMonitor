# MiniMonitor for Windows

MiniMonitor is a compact Windows sidebar resource monitor inspired by the macOS LightMonitor app found in `C:\Users\陈 华 宇\Downloads\ColrfulMonitr`.

It is a native Win32 application with no runtime dependency beyond Windows itself.

## Features

- Compact sidebar-style panel with soft metric cards and live sparklines
- CPU, memory, disk capacity, disk I/O, and network throughput
- GPU adapter display when Windows exposes display-device information
- Top CPU, memory, and GPU process summaries
- Optional Codex quota card using the local Codex ChatGPT sign-in token
- System tray hover summary for CPU, GPU, memory, network, and Codex quota
- Rich tray menu with status details, copy summary, manual refresh, quota refresh, Task Manager and Resource Monitor shortcuts, high-usage alerts with adjustable threshold, global hotkey toggle, pause/resume, refresh interval, opacity control, window lock, always-on-top, position reset, startup-hidden toggle, and auto-start toggle
- Single-instance startup with remembered window position
- Quick controls: tray left-click toggles the panel, `Ctrl+Shift+M` toggles it globally, `Esc` hides it, `Space` pauses/resumes refresh, `F5` refreshes now, and `Ctrl+C` copies status
- Single-file executable build output

## Build

```powershell
.\build.ps1
```

The executable is written to:

```text
dist\MiniMonitor.exe
```

## Run

Double-click `dist\MiniMonitor.exe`.

When closed or minimized, the app hides to the Windows system tray. Left-click the tray icon to show or hide the panel. Right-click it to refresh immediately, enable high-usage alerts, adjust alert threshold, refresh interval and opacity, toggle the global hotkey, lock the panel position, open Task Manager or Resource Monitor, copy a current status summary, refresh Codex quota, pause updates, keep the panel on top, reset the panel position, or toggle startup behavior.
