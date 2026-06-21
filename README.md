# MiniMonitor for Windows

MiniMonitor is a compact Windows sidebar resource monitor inspired by lightweight macOS-style system monitor widgets.

It is a native Win32 application with no runtime dependency beyond Windows itself.

## Features

- Compact sidebar-style panel with soft metric cards and live sparklines
- CPU, memory, disk capacity, disk I/O, and network throughput
- GPU adapter display when Windows exposes display-device information
- Top CPU, memory, and GPU process summaries
- Optional Codex quota card using the local Codex ChatGPT sign-in token
- System tray hover summary for CPU, GPU, memory, network, and Codex quota
- Grouped tray menu with compact top-level actions and nested status, report, refresh, display, alert, tool, and startup settings
- Four selectable UI themes: Mono, Ocean, Sakura, and Forest, with the choice saved between launches
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

When closed or minimized, the app hides to the Windows system tray. Left-click the tray icon to show or hide the panel. Right-click it to refresh immediately, refresh Codex quota, open grouped status/report/settings menus, switch themes, tune refresh and alert behavior, open system tools, or change startup behavior.
