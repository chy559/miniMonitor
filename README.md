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
- Rich tray menu with status details, copy summary, quota refresh, position reset, startup-hidden toggle, and auto-start toggle
- Single-instance startup with remembered window position
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

When minimized, the app hides to the Windows system tray. Right-click the tray icon to show the panel, copy a current status summary, refresh Codex quota, reset the panel position, or toggle startup behavior.
