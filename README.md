# Colorful Monitor for Windows

Colorful Monitor is a lightweight Windows resource monitor inspired by the macOS LightMonitor app found in `C:\Users\陈 华 宇\Downloads\ColrfulMonitr`.

It is a native Win32 application with no runtime dependency beyond Windows itself.

## Features

- Polished dark resource dashboard with rounded metric cards and live sparklines
- CPU, memory, disk capacity, disk I/O, and network throughput
- GPU adapter display when Windows exposes display-device information
- Top memory process list
- System tray integration with show / exit menu
- Single-file executable build output

## Build

```powershell
.\build.ps1
```

The executable is written to:

```text
dist\ColorfulMonitor.exe
```

## Run

Double-click `dist\ColorfulMonitor.exe`.

When minimized, the app hides to the Windows system tray. Right-click the tray icon to show or exit.
