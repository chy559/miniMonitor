#pragma once

#include <windows.h>

inline constexpr UINT_PTR kRefreshTimer = 1001;
inline constexpr UINT kTrayMessage = WM_APP + 42;
inline constexpr UINT kMetricsReadyMessage = WM_APP + 43;
inline constexpr UINT kQuotaReadyMessage = WM_APP + 44;

inline constexpr UINT kMenuShow = 1;
inline constexpr UINT kMenuExit = 2;
inline constexpr UINT kMenuHide = 3;
inline constexpr UINT kMenuRefreshQuota = 4;
inline constexpr UINT kMenuCopyStatus = 5;
inline constexpr UINT kMenuToggleStartHidden = 6;
inline constexpr UINT kMenuToggleAutoStart = 7;
inline constexpr UINT kMenuResetPosition = 8;
inline constexpr UINT kMenuToggleAlwaysOnTop = 9;
inline constexpr UINT kMenuTogglePause = 10;
inline constexpr UINT kMenuRefreshNow = 11;
inline constexpr UINT kMenuRefreshInterval1s = 12;
inline constexpr UINT kMenuRefreshInterval2s = 13;
inline constexpr UINT kMenuRefreshInterval5s = 14;
inline constexpr UINT kMenuOpenTaskManager = 15;
inline constexpr UINT kMenuToggleLockPosition = 16;
inline constexpr UINT kMenuOpacity100 = 17;
inline constexpr UINT kMenuOpacity90 = 18;
inline constexpr UINT kMenuOpacity80 = 19;
inline constexpr UINT kMenuToggleHighUsageAlerts = 20;
inline constexpr UINT kMenuAlertThreshold80 = 21;
inline constexpr UINT kMenuAlertThreshold90 = 22;
inline constexpr UINT kMenuAlertThreshold95 = 23;
inline constexpr UINT kMenuOpenResourceMonitor = 24;
inline constexpr UINT kMenuToggleGlobalHotkey = 25;
inline constexpr UINT kMenuToggleBackgroundEcoMode = 26;
inline constexpr UINT kMenuOpenAppFolder = 27;
inline constexpr UINT kMenuResetSettings = 28;
inline constexpr UINT kMenuExportStatusReport = 29;
inline constexpr UINT kMenuOpenReportsFolder = 30;
inline constexpr UINT kMenuClearReports = 31;
inline constexpr UINT kMenuThemeMono = 32;
inline constexpr UINT kMenuThemeOcean = 33;
inline constexpr UINT kMenuThemeSakura = 34;
inline constexpr UINT kMenuThemeForest = 35;
inline constexpr UINT kMenuDiskBase = 1000;
inline constexpr UINT kMenuDiskLast = kMenuDiskBase + 25;

inline constexpr int kAppIconResource = 101;
inline constexpr wchar_t kClassName[] = L"MiniMonitorWindow";
inline constexpr wchar_t kAppTitle[] = L"MiniMonitor";
inline constexpr wchar_t kSingletonMutexName[] = L"MiniMonitor.Singleton";
inline constexpr int kPanelWidth = 430;
inline constexpr int kPanelHeight = 820;
inline constexpr int kHotkeyTogglePanel = 2001;
inline constexpr UINT kDefaultRefreshIntervalMs = 2000;
inline constexpr UINT kBackgroundEcoRefreshIntervalMs = 10000;
inline constexpr DWORD kDefaultHighUsageAlertThreshold = 90;
inline constexpr ULONGLONG kHighUsageAlertCooldownMs = 5 * 60 * 1000;
