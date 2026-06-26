#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winhttp.h>
#include <shellapi.h>

#include "app_window.h"
#include "app_models.h"
#include "app_paths.h"
#include "codex_quota_client.h"
#include "format_utils.h"
#include "settings_store.h"
#include "system_sampler.h"
#include "ui_theme.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;
namespace {

constexpr UINT_PTR kRefreshTimer = 1001;
constexpr UINT kTrayMessage = WM_APP + 42;
constexpr UINT kMetricsReadyMessage = WM_APP + 43;
constexpr UINT kQuotaReadyMessage = WM_APP + 44;
constexpr UINT kMenuShow = 1;
constexpr UINT kMenuExit = 2;
constexpr UINT kMenuHide = 3;
constexpr UINT kMenuRefreshQuota = 4;
constexpr UINT kMenuCopyStatus = 5;
constexpr UINT kMenuToggleStartHidden = 6;
constexpr UINT kMenuToggleAutoStart = 7;
constexpr UINT kMenuResetPosition = 8;
constexpr UINT kMenuToggleAlwaysOnTop = 9;
constexpr UINT kMenuTogglePause = 10;
constexpr UINT kMenuRefreshNow = 11;
constexpr UINT kMenuRefreshInterval1s = 12;
constexpr UINT kMenuRefreshInterval2s = 13;
constexpr UINT kMenuRefreshInterval5s = 14;
constexpr UINT kMenuOpenTaskManager = 15;
constexpr UINT kMenuToggleLockPosition = 16;
constexpr UINT kMenuOpacity100 = 17;
constexpr UINT kMenuOpacity90 = 18;
constexpr UINT kMenuOpacity80 = 19;
constexpr UINT kMenuToggleHighUsageAlerts = 20;
constexpr UINT kMenuAlertThreshold80 = 21;
constexpr UINT kMenuAlertThreshold90 = 22;
constexpr UINT kMenuAlertThreshold95 = 23;
constexpr UINT kMenuOpenResourceMonitor = 24;
constexpr UINT kMenuToggleGlobalHotkey = 25;
constexpr UINT kMenuToggleBackgroundEcoMode = 26;
constexpr UINT kMenuOpenAppFolder = 27;
constexpr UINT kMenuResetSettings = 28;
constexpr UINT kMenuExportStatusReport = 29;
constexpr UINT kMenuOpenReportsFolder = 30;
constexpr UINT kMenuClearReports = 31;
constexpr UINT kMenuThemeMono = 32;
constexpr UINT kMenuThemeOcean = 33;
constexpr UINT kMenuThemeSakura = 34;
constexpr UINT kMenuThemeForest = 35;
constexpr UINT kMenuDiskBase = 1000;
constexpr UINT kMenuDiskLast = kMenuDiskBase + 25;
constexpr int kAppIconResource = 101;
constexpr wchar_t kClassName[] = L"MiniMonitorWindow";
constexpr wchar_t kAppTitle[] = L"MiniMonitor";
constexpr wchar_t kSingletonMutexName[] = L"MiniMonitor.Singleton";
constexpr int kPanelWidth = 430;
constexpr int kPanelHeight = 820;
constexpr int kHotkeyTogglePanel = 2001;
constexpr UINT kDefaultRefreshIntervalMs = 2000;
constexpr UINT kBackgroundEcoRefreshIntervalMs = 10000;
constexpr DWORD kDefaultHighUsageAlertThreshold = 90;
constexpr ULONGLONG kHighUsageAlertCooldownMs = 5 * 60 * 1000;

std::wstring computerName() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buffer, &size)) {
        return buffer;
    }
    return L"Windows PC";
}

void enableDpiAwareness() {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32) {
        return;
    }

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    auto dpiContextProc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    SetProcessDpiAwarenessContextFn setDpiContext = nullptr;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    setDpiContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(dpiContextProc);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (setDpiContext && setDpiContext(reinterpret_cast<HANDLE>(-4))) {
        FreeLibrary(user32);
        return;
    }

    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    auto dpiAwareProc = GetProcAddress(user32, "SetProcessDPIAware");
    SetProcessDPIAwareFn setDpiAware = nullptr;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    setDpiAware = reinterpret_cast<SetProcessDPIAwareFn>(dpiAwareProc);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (setDpiAware) {
        setDpiAware();
    }
    FreeLibrary(user32);
}

UINT systemDpi() {
    HDC screen = GetDC(nullptr);
    if (!screen) {
        return 96;
    }
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

std::unique_ptr<GraphicsPath> roundedRect(RectF rect, REAL radius) {
    auto path = std::make_unique<GraphicsPath>();
    REAL d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180, 90);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    path->CloseFigure();
    return path;
}

HICON createAppIcon() {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    auto old = SelectObject(dc, color);

    TRIVERTEX verts[2] = {
        {0, 0, 0x1700, 0x2400, 0x3a00, 0xffff},
        {32, 32, 0x6800, 0xd800, 0xc200, 0xffff},
    };
    GRADIENT_RECT gRect{0, 1};
    GradientFill(dc, verts, 2, &gRect, 1, GRADIENT_FILL_RECT_H);

    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush glow(Color(230, 255, 255, 255));
    SolidBrush dark(Color(220, 10, 20, 35));
    Pen pen(Color(240, 255, 255, 255), 2.0f);
    graphics.FillEllipse(&dark, 5, 5, 22, 22);
    graphics.DrawEllipse(&pen, 5, 5, 22, 22);
    graphics.FillEllipse(&glow, 12, 10, 8, 8);

    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

HICON loadAppIcon(HINSTANCE instance) {
    auto icon = reinterpret_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(kAppIconResource), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    return icon ? icon : createAppIcon();
}

class AppWindow {
public:
    AppWindow(HINSTANCE instance) : instance_(instance), icon_(loadAppIcon(instance)) {}

    ~AppWindow() {
        stopWorkers();
        removeTrayIcon();
        if (icon_) {
            DestroyIcon(icon_);
        }
    }

    bool create() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpszClassName = kClassName;
        wc.lpfnWndProc = AppWindow::windowProc;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = icon_;
        wc.hIconSm = icon_;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

        if (!RegisterClassExW(&wc)) {
            return false;
        }

        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        dpi_ = systemDpi();
        const int panelWidth = scalePx(kPanelWidth);
        const int availableHeight = static_cast<int>(workArea.bottom - workArea.top - scalePx(32));
        const int panelHeight = std::min(scalePx(kPanelHeight), std::max(scalePx(560), availableHeight));
        startHidden_ = readBoolSetting(L"StartHidden", false);
        alwaysOnTop_ = readBoolSetting(L"AlwaysOnTop", false);
        paused_ = readBoolSetting(L"Paused", false);
        lockPosition_ = readBoolSetting(L"LockPosition", false);
        highUsageAlerts_ = readBoolSetting(L"HighUsageAlerts", false);
        globalHotkeyEnabled_ = readBoolSetting(L"GlobalHotkeyEnabled", true);
        backgroundEcoMode_ = readBoolSetting(L"BackgroundEcoMode", false);
        highUsageAlertThreshold_ = sanitizeAlertThreshold(readDwordSetting(L"HighUsageAlertThreshold", kDefaultHighUsageAlertThreshold));
        refreshIntervalMs_ = sanitizeRefreshInterval(readDwordSetting(L"RefreshIntervalMs", kDefaultRefreshIntervalMs));
        windowOpacity_ = sanitizeWindowOpacity(readDwordSetting(L"WindowOpacity", 255));
        theme_ = sanitizeTheme(readDwordSetting(L"Theme", static_cast<DWORD>(UiTheme::Mono)));
        diskDriveLetter_ = sanitizeDiskDrive(readDwordSetting(L"DiskDrive", static_cast<DWORD>(systemDriveLetter())));
        POINT panelPos = startupPanelPosition(workArea, panelWidth, panelHeight);

        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_LAYERED | (alwaysOnTop_ ? WS_EX_TOPMOST : 0),
            kClassName,
            kAppTitle,
            WS_POPUP,
            panelPos.x,
            panelPos.y,
            panelWidth,
            panelHeight,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return false;
        }

        applyWindowRegion();
        applyWindowOpacity();
        applyGlobalHotkey(false);
        addTrayIcon();
        updateTrayTip();
        startWorkers();
        requestMetricsSample(false);
        if (startHidden_) {
            hidePanel();
        } else {
            showPanel(false);
        }
        applyRefreshTimer();
        showStartupNotice(!readBoolSetting(L"OnboardingShown", false));
        startCodexQuotaRefresh(false, false);
        return true;
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    UINT dpi_ = 96;
    Metrics metrics_;
    std::wstring machineName_ = computerName();
    SampleHistory cpuHistory_;
    SampleHistory gpuHistory_;
    SampleHistory memoryHistory_;
    SampleHistory diskHistory_;
    SampleHistory networkHistory_;
    bool startHidden_ = false;
    bool alwaysOnTop_ = false;
    bool paused_ = false;
    bool lockPosition_ = false;
    bool highUsageAlerts_ = false;
    bool globalHotkeyEnabled_ = true;
    bool globalHotkeyRegistered_ = false;
    bool backgroundEcoMode_ = false;
    UiTheme theme_ = UiTheme::Mono;
    wchar_t diskDriveLetter_ = L'C';
    DWORD highUsageAlertThreshold_ = kDefaultHighUsageAlertThreshold;
    UINT refreshIntervalMs_ = kDefaultRefreshIntervalMs;
    BYTE windowOpacity_ = 255;
    ULONGLONG lastHighUsageAlertTick_ = 0;
    bool quotaRefreshInProgress_ = false;
    std::thread metricsWorker_;
    std::thread quotaWorker_;
    std::mutex workerMutex_;
    std::condition_variable metricsCv_;
    std::condition_variable quotaCv_;
    bool workersStarted_ = false;
    bool workersStopping_ = false;
    bool metricsRequested_ = false;
    bool metricsAnnounceRequested_ = false;
    bool quotaRequested_ = false;
    bool quotaForceRequested_ = false;
    bool quotaAnnounceRequested_ = false;
    bool readyMetricsAnnounce_ = false;
    bool readyQuotaAnnounce_ = false;
    std::wstring workerDiskRoot_ = L"C:\\";
    SettingsStore settings_;
    std::unique_ptr<Metrics> readyMetrics_;
    std::unique_ptr<CodexQuota> readyQuota_;

    static AppWindow* fromWindow(HWND hwnd) {
        return reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto self = reinterpret_cast<AppWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }

        auto self = fromWindow(hwnd);
        if (!self) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        return self->handleMessage(message, wParam, lParam);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                if (paused_) {
                    return 0;
                }
                requestMetricsSample(false);
            }
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_NCHITTEST:
            return hitTest(lParam);
        case WM_LBUTTONUP:
            handleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYDOWN:
            handleKeyDown(wParam);
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                hidePanel();
            } else {
                applyWindowRegion();
                applyRefreshTimer();
            }
            return 0;
        case WM_DPICHANGED: {
            dpi_ = std::max<UINT>(96, HIWORD(wParam));
            const auto suggested = reinterpret_cast<RECT*>(lParam);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            HMONITOR monitor = MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST);
            RECT workArea = *suggested;
            if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
                workArea = monitorInfo.rcWork;
            }
            const int width = scalePx(kPanelWidth);
            const int availableHeight = static_cast<int>(workArea.bottom - workArea.top - scalePx(32));
            const int height = std::min(scalePx(kPanelHeight), std::max(scalePx(560), availableHeight));
            const int maxX = std::max(static_cast<int>(workArea.left), static_cast<int>(workArea.right) - width);
            const int maxY = std::max(static_cast<int>(workArea.top), static_cast<int>(workArea.bottom) - height);
            const int x = std::clamp(static_cast<int>(suggested->left), static_cast<int>(workArea.left), maxX);
            const int y = std::clamp(static_cast<int>(suggested->top), static_cast<int>(workArea.top), maxY);
            SetWindowPos(hwnd_, nullptr, x, y, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            applyWindowRegion();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        case WM_EXITSIZEMOVE:
            saveWindowPosition();
            return 0;
        case WM_CLOSE:
            hidePanel();
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyTogglePanel) {
                togglePanelVisibility();
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuShow) {
                showPanel();
            } else if (LOWORD(wParam) == kMenuExit) {
                DestroyWindow(hwnd_);
            } else if (LOWORD(wParam) == kMenuHide) {
                hidePanel();
            } else if (LOWORD(wParam) == kMenuRefreshQuota) {
                refreshCodexQuotaNow();
            } else if (LOWORD(wParam) == kMenuCopyStatus) {
                copyStatusToClipboard();
            } else if (LOWORD(wParam) == kMenuToggleStartHidden) {
                toggleStartHidden();
            } else if (LOWORD(wParam) == kMenuToggleAutoStart) {
                toggleAutoStart();
            } else if (LOWORD(wParam) == kMenuResetPosition) {
                resetWindowPosition();
            } else if (LOWORD(wParam) == kMenuToggleAlwaysOnTop) {
                toggleAlwaysOnTop();
            } else if (LOWORD(wParam) == kMenuTogglePause) {
                togglePause();
            } else if (LOWORD(wParam) == kMenuRefreshNow) {
                refreshNow(true);
            } else if (LOWORD(wParam) == kMenuRefreshInterval1s) {
                setRefreshInterval(1000);
            } else if (LOWORD(wParam) == kMenuRefreshInterval2s) {
                setRefreshInterval(2000);
            } else if (LOWORD(wParam) == kMenuRefreshInterval5s) {
                setRefreshInterval(5000);
            } else if (LOWORD(wParam) == kMenuOpenTaskManager) {
                openTaskManager();
            } else if (LOWORD(wParam) == kMenuToggleLockPosition) {
                toggleLockPosition();
            } else if (LOWORD(wParam) == kMenuOpacity100) {
                setWindowOpacity(255);
            } else if (LOWORD(wParam) == kMenuOpacity90) {
                setWindowOpacity(230);
            } else if (LOWORD(wParam) == kMenuOpacity80) {
                setWindowOpacity(205);
            } else if (LOWORD(wParam) == kMenuToggleHighUsageAlerts) {
                toggleHighUsageAlerts();
            } else if (LOWORD(wParam) == kMenuAlertThreshold80) {
                setHighUsageAlertThreshold(80);
            } else if (LOWORD(wParam) == kMenuAlertThreshold90) {
                setHighUsageAlertThreshold(90);
            } else if (LOWORD(wParam) == kMenuAlertThreshold95) {
                setHighUsageAlertThreshold(95);
            } else if (LOWORD(wParam) == kMenuOpenResourceMonitor) {
                openResourceMonitor();
            } else if (LOWORD(wParam) == kMenuToggleGlobalHotkey) {
                toggleGlobalHotkey();
            } else if (LOWORD(wParam) == kMenuToggleBackgroundEcoMode) {
                toggleBackgroundEcoMode();
            } else if (LOWORD(wParam) == kMenuOpenAppFolder) {
                openAppFolder();
            } else if (LOWORD(wParam) == kMenuResetSettings) {
                resetAppSettings();
            } else if (LOWORD(wParam) == kMenuExportStatusReport) {
                exportStatusReport();
            } else if (LOWORD(wParam) == kMenuOpenReportsFolder) {
                openReportsFolder();
            } else if (LOWORD(wParam) == kMenuClearReports) {
                clearReports();
            } else if (LOWORD(wParam) == kMenuThemeMono) {
                setTheme(UiTheme::Mono);
            } else if (LOWORD(wParam) == kMenuThemeOcean) {
                setTheme(UiTheme::Ocean);
            } else if (LOWORD(wParam) == kMenuThemeSakura) {
                setTheme(UiTheme::Sakura);
            } else if (LOWORD(wParam) == kMenuThemeForest) {
                setTheme(UiTheme::Forest);
            } else if (LOWORD(wParam) >= kMenuDiskBase && LOWORD(wParam) <= kMenuDiskLast) {
                setDiskDrive(static_cast<wchar_t>(L'A' + LOWORD(wParam) - kMenuDiskBase));
            }
            return 0;
        case kTrayMessage:
            handleTrayMessage(lParam);
            return 0;
        case kMetricsReadyMessage:
            handleMetricsReady();
            return 0;
        case kQuotaReadyMessage:
            handleQuotaRefreshComplete();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kRefreshTimer);
            unregisterGlobalHotkey();
            removeTrayIcon();
            stopWorkers();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    void updateHistory() {
        cpuHistory_.push(metrics_.cpu);
        gpuHistory_.push(metrics_.gpu >= 0.0 ? metrics_.gpu : 0.0);
        memoryHistory_.push(metrics_.memory);
        diskHistory_.push(metrics_.disk);
        networkHistory_.push(metrics_.network);
    }

    REAL dpiScale() const {
        return std::min(1.0f, static_cast<REAL>(dpi_) / 96.0f);
    }

    int scalePx(int logical) const {
        return static_cast<int>(std::round(static_cast<REAL>(logical) * dpiScale()));
    }

    int unscalePx(int physical) const {
        return static_cast<int>(std::round(static_cast<REAL>(physical) / dpiScale()));
    }

    void applyWindowRegion() {
        if (!hwnd_) {
            return;
        }
        RECT client{};
        if (!GetClientRect(hwnd_, &client)) {
            return;
        }
        const int radius = scalePx(24);
        HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, radius, radius);
        if (!SetWindowRgn(hwnd_, region, TRUE)) {
            DeleteObject(region);
        }
    }

    void startWorkers() {
        if (workersStarted_) {
            return;
        }
        workersStarted_ = true;
        workerDiskRoot_ = diskRootPath();

        metricsWorker_ = std::thread([this]() {
            SystemSampler sampler;
            std::wstring activeDiskRoot;
            for (;;) {
                bool announce = false;
                std::wstring diskRoot;
                {
                    std::unique_lock<std::mutex> lock(workerMutex_);
                    metricsCv_.wait(lock, [this]() {
                        return workersStopping_ || metricsRequested_;
                    });
                    if (workersStopping_) {
                        return;
                    }
                    metricsRequested_ = false;
                    announce = metricsAnnounceRequested_;
                    metricsAnnounceRequested_ = false;
                    diskRoot = workerDiskRoot_;
                }

                if (activeDiskRoot != diskRoot) {
                    sampler.setDiskRoot(diskRoot);
                    activeDiskRoot = diskRoot;
                }
                Metrics result = sampler.collect();

                {
                    std::lock_guard<std::mutex> lock(workerMutex_);
                    if (workersStopping_) {
                        return;
                    }
                    readyMetrics_ = std::make_unique<Metrics>(std::move(result));
                    readyMetricsAnnounce_ = readyMetricsAnnounce_ || announce;
                }
                PostMessageW(hwnd_, kMetricsReadyMessage, 0, 0);
            }
        });

        quotaWorker_ = std::thread([this]() {
            CodexQuotaClient client;
            for (;;) {
                bool forceRefresh = false;
                bool announce = false;
                {
                    std::unique_lock<std::mutex> lock(workerMutex_);
                    quotaCv_.wait(lock, [this]() {
                        return workersStopping_ || quotaRequested_;
                    });
                    if (workersStopping_) {
                        return;
                    }
                    quotaRequested_ = false;
                    forceRefresh = quotaForceRequested_;
                    announce = quotaAnnounceRequested_;
                    quotaForceRequested_ = false;
                    quotaAnnounceRequested_ = false;
                }

                CodexQuota result = client.fetch(forceRefresh);
                {
                    std::lock_guard<std::mutex> lock(workerMutex_);
                    if (workersStopping_) {
                        return;
                    }
                    readyQuota_ = std::make_unique<CodexQuota>(std::move(result));
                    readyQuotaAnnounce_ = readyQuotaAnnounce_ || announce;
                }
                PostMessageW(hwnd_, kQuotaReadyMessage, 0, 0);
            }
        });
    }

    void stopWorkers() {
        if (!workersStarted_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workersStopping_ = true;
            metricsRequested_ = false;
            quotaRequested_ = false;
        }
        metricsCv_.notify_all();
        quotaCv_.notify_all();
        if (metricsWorker_.joinable()) {
            metricsWorker_.join();
        }
        if (quotaWorker_.joinable()) {
            quotaWorker_.join();
        }
        workersStarted_ = false;
        readyMetrics_.reset();
        readyQuota_.reset();
    }

    void requestMetricsSample(bool announce) {
        if (!workersStarted_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (workersStopping_) {
                return;
            }
            workerDiskRoot_ = diskRootPath();
            metricsRequested_ = true;
            metricsAnnounceRequested_ = metricsAnnounceRequested_ || announce;
        }
        metricsCv_.notify_one();
    }

    void handleMetricsReady() {
        std::unique_ptr<Metrics> result;
        bool announce = false;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            result = std::move(readyMetrics_);
            announce = readyMetricsAnnounce_;
            readyMetricsAnnounce_ = false;
        }
        if (!result) {
            return;
        }

        CodexQuota quota = metrics_.quota;
        metrics_ = std::move(*result);
        metrics_.quota = std::move(quota);
        updateHistory();
        checkHighUsageAlerts();
        updateTrayTip();
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (announce) {
            showTrayBalloon(L"MiniMonitor", L"状态已刷新。");
        }
    }

    bool readBoolSetting(const wchar_t* name, bool fallback) {
        return settings_.readBool(name, fallback);
    }

    void writeBoolSetting(const wchar_t* name, bool value) {
        settings_.writeBool(name, value);
    }

    DWORD readDwordSetting(const wchar_t* name, DWORD fallback) {
        return settings_.readDword(name, fallback);
    }

    void writeDwordSetting(const wchar_t* name, DWORD value) {
        settings_.writeDword(name, value);
    }

    UINT sanitizeRefreshInterval(DWORD value) {
        if (value == 1000 || value == 2000 || value == 5000) {
            return static_cast<UINT>(value);
        }
        return kDefaultRefreshIntervalMs;
    }

    BYTE sanitizeWindowOpacity(DWORD value) {
        if (value == 255 || value == 230 || value == 205) {
            return static_cast<BYTE>(value);
        }
        return 255;
    }

    UiTheme sanitizeTheme(DWORD value) {
        if (value <= static_cast<DWORD>(UiTheme::Forest)) {
            return static_cast<UiTheme>(value);
        }
        return UiTheme::Mono;
    }

    ThemePalette theme() const {
        return paletteForTheme(theme_);
    }
    std::wstring themeName() const {
        return theme().name;
    }

    wchar_t systemDriveLetter() {
        wchar_t windowsDir[MAX_PATH]{};
        if (GetWindowsDirectoryW(windowsDir, MAX_PATH) > 0 && iswalpha(windowsDir[0])) {
            return static_cast<wchar_t>(towupper(windowsDir[0]));
        }
        return L'C';
    }

    wchar_t sanitizeDiskDrive(DWORD value) {
        wchar_t letter = static_cast<wchar_t>(towupper(static_cast<wint_t>(value)));
        if (letter < L'A' || letter > L'Z') {
            letter = systemDriveLetter();
        }

        DWORD drives = GetLogicalDrives();
        if (drives != 0 && (drives & (1u << (letter - L'A'))) == 0) {
            letter = systemDriveLetter();
        }
        return letter;
    }

    std::wstring diskRootPath() const {
        std::wstring root;
        root.push_back(diskDriveLetter_);
        root += L":\\";
        return root;
    }

    std::wstring diskDisplayName() const {
        std::wstring name;
        name.push_back(diskDriveLetter_);
        name += L":";
        return name;
    }

    DWORD sanitizeAlertThreshold(DWORD value) {
        if (value == 80 || value == 90 || value == 95) {
            return value;
        }
        return kDefaultHighUsageAlertThreshold;
    }

    std::wstring autoStartCommand() {
        std::wstring path = executablePath();
        if (path.empty()) {
            return L"";
        }
        return L"\"" + path + L"\"";
    }

    bool isAutoStartEnabled() {
        return settings_.isAutoStartEnabled();
    }

    bool setAutoStart(bool enabled) {
        return settings_.setAutoStart(enabled, autoStartCommand());
    }

    POINT startupPanelPosition(const RECT& workArea, int panelWidth, int panelHeight) {
        POINT fallback{workArea.right - panelWidth - scalePx(16), workArea.top + scalePx(16)};
        int x = 0;
        int y = 0;
        if (!readSavedPosition(x, y)) {
            return fallback;
        }

        RECT targetWorkArea = workArea;
        const POINT savedPoint{x, y};
        HMONITOR monitor = MonitorFromPoint(savedPoint, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
            targetWorkArea = monitorInfo.rcWork;
        }

        const int minX = targetWorkArea.left;
        const int minY = targetWorkArea.top;
        const int maxX = std::max(minX, static_cast<int>(targetWorkArea.right) - panelWidth);
        const int maxY = std::max(minY, static_cast<int>(targetWorkArea.bottom) - panelHeight);
        return POINT{std::clamp(x, minX, maxX), std::clamp(y, minY, maxY)};
    }

    bool readSavedPosition(int& x, int& y) {
        return settings_.readWindowPosition(x, y);
    }

    void saveWindowPosition() {
        if (!hwnd_) {
            return;
        }
        RECT rect{};
        if (!GetWindowRect(hwnd_, &rect)) {
            return;
        }
        settings_.writeWindowPosition(rect.left, rect.top);
    }

    void addTrayIcon() {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = icon_;
        wcscpy_s(nid.szTip, trayTipText().c_str());
        Shell_NotifyIconW(NIM_ADD, &nid);
    }

    void updateTrayTip() {
        if (!hwnd_) {
            return;
        }
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_TIP;
        wcscpy_s(nid.szTip, trayTipText().c_str());
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void showStartupNotice(bool firstRun) {
        if (startHidden_) {
            showTrayBalloon(L"MiniMonitor 正在后台运行",
                            L"左键托盘图标显示面板，右键可查看资源详情、刷新 Codex 额度或调整启动设置。");
        } else if (firstRun) {
            showTrayBalloon(L"欢迎使用 MiniMonitor",
                            L"左键托盘图标可隐藏/显示面板，右键可查看更多状态和快捷操作。");
        }

        if (firstRun) {
            writeBoolSetting(L"OnboardingShown", true);
        }
    }

    void removeTrayIcon() {
        if (!hwnd_) {
            return;
        }
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void handleTrayMessage(LPARAM lParam) {
        if (lParam == WM_LBUTTONUP) {
            togglePanelVisibility();
        } else if (lParam == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            populateTrayMenu(menu);
            SetForegroundWindow(hwnd_);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
            DestroyMenu(menu);
        }
    }

    void togglePanelVisibility() {
        if (IsWindowVisible(hwnd_)) {
            hidePanel();
        } else {
            showPanel();
        }
    }

    void showPanel(bool activate = true) {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(hwnd_, SW_RESTORE);
        applyRefreshTimer();
        UpdateWindow(hwnd_);
        if (activate) {
            SetForegroundWindow(hwnd_);
        }
    }

    void hidePanel() {
        ShowWindow(hwnd_, SW_HIDE);
        applyRefreshTimer();
    }

    void handleKeyDown(WPARAM key) {
        if (key == VK_ESCAPE) {
            hidePanel();
        } else if (key == VK_SPACE) {
            togglePause();
        } else if (key == VK_F5) {
            refreshNow(true);
        } else if ((key == L'C' || key == L'c') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            copyStatusToClipboard();
        }
    }

    void populateTrayMenu(HMENU menu) {
        if (!paused_) {
            requestMetricsSample(false);
        }

        HMENU statusMenu = CreatePopupMenu();
        appendInfoItem(menu, windowStateText());
        appendInfoItem(menu, L"CPU " + formatPercent(metrics_.cpu) +
                             L"    MEM " + formatPercent(metrics_.memory) +
                             L"    GPU " + (metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuShow, L"显示面板");
        AppendMenuW(menu, MF_STRING, kMenuHide, L"隐藏到后台");
        AppendMenuW(menu, MF_STRING, kMenuRefreshNow, L"立即刷新");
        AppendMenuW(menu, MF_STRING, kMenuRefreshQuota, L"刷新 Codex 额度");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        appendInfoItem(statusMenu, L"CPU " + formatPercent(metrics_.cpu) +
                                   L"    GPU " + (metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A"));
        appendInfoItem(statusMenu, L"内存 " + formatPercent(metrics_.memory) + L"  " +
                                   formatBytes(static_cast<double>(metrics_.memoryUsed)) + L" / " +
                                   formatBytes(static_cast<double>(metrics_.memoryTotal)));
        appendInfoItem(statusMenu, L"网络 ↓ " + formatSpeed(metrics_.netDown) + L"    ↑ " + formatSpeed(metrics_.netUp));
        appendInfoItem(statusMenu, L"磁盘 " + diskDisplayName() + L" Read " + (metrics_.diskRead >= 0 ? formatSpeed(metrics_.diskRead) : L"N/A") +
                                   L"    Write " + (metrics_.diskWrite >= 0 ? formatSpeed(metrics_.diskWrite) : L"N/A"));
        appendInfoItem(statusMenu, trayQuotaText());
        appendInfoItem(statusMenu, trayTopProcessText());
        appendInfoItem(statusMenu, trayTopMemoryText());
        appendInfoItem(statusMenu, trayTopGpuText());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(statusMenu), L"状态详情");

        HMENU reportMenu = CreatePopupMenu();
        AppendMenuW(reportMenu, MF_STRING, kMenuCopyStatus, L"复制当前状态");
        AppendMenuW(reportMenu, MF_STRING, kMenuExportStatusReport, L"导出状态报告");
        AppendMenuW(reportMenu, MF_STRING, kMenuOpenReportsFolder, L"打开报告目录");
        AppendMenuW(reportMenu, MF_STRING, kMenuClearReports, L"清理状态报告");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(reportMenu), L"报告与复制");

        HMENU refreshMenu = CreatePopupMenu();
        appendInfoItem(refreshMenu, L"当前刷新间隔 " + refreshIntervalText());
        appendInfoItem(refreshMenu, L"后台刷新 " + backgroundRefreshText());
        AppendMenuW(refreshMenu, MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED), kMenuTogglePause, paused_ ? L"继续刷新" : L"暂停刷新");
        AppendMenuW(refreshMenu, MF_STRING | (backgroundEcoMode_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleBackgroundEcoMode, L"后台低频刷新");
        AppendMenuW(refreshMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(refreshMenu, MF_STRING | (refreshIntervalMs_ == 1000 ? MF_CHECKED : MF_UNCHECKED), kMenuRefreshInterval1s, L"1 秒");
        AppendMenuW(refreshMenu, MF_STRING | (refreshIntervalMs_ == 2000 ? MF_CHECKED : MF_UNCHECKED), kMenuRefreshInterval2s, L"2 秒");
        AppendMenuW(refreshMenu, MF_STRING | (refreshIntervalMs_ == 5000 ? MF_CHECKED : MF_UNCHECKED), kMenuRefreshInterval5s, L"5 秒");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(refreshMenu), L"刷新设置");

        HMENU displayMenu = CreatePopupMenu();
        appendInfoItem(displayMenu, L"窗口透明度 " + opacityText());
        appendInfoItem(displayMenu, L"当前主题 " + themeName());
        appendInfoItem(displayMenu, L"监控磁盘 " + diskDisplayName());
        AppendMenuW(displayMenu, MF_STRING | (alwaysOnTop_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleAlwaysOnTop, L"窗口置顶");
        AppendMenuW(displayMenu, MF_STRING | (lockPosition_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleLockPosition, L"锁定窗口位置");
        AppendMenuW(displayMenu, MF_SEPARATOR, 0, nullptr);
        HMENU diskMenu = CreatePopupMenu();
        populateDiskMenu(diskMenu);
        AppendMenuW(displayMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(diskMenu), L"监控磁盘");
        HMENU themeMenu = CreatePopupMenu();
        AppendMenuW(themeMenu, MF_STRING | (theme_ == UiTheme::Mono ? MF_CHECKED : MF_UNCHECKED), kMenuThemeMono, L"黑白简约");
        AppendMenuW(themeMenu, MF_STRING | (theme_ == UiTheme::Ocean ? MF_CHECKED : MF_UNCHECKED), kMenuThemeOcean, L"海盐蓝");
        AppendMenuW(themeMenu, MF_STRING | (theme_ == UiTheme::Sakura ? MF_CHECKED : MF_UNCHECKED), kMenuThemeSakura, L"樱雾粉");
        AppendMenuW(themeMenu, MF_STRING | (theme_ == UiTheme::Forest ? MF_CHECKED : MF_UNCHECKED), kMenuThemeForest, L"森林绿");
        AppendMenuW(displayMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"主题");
        AppendMenuW(displayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(displayMenu, MF_STRING | (windowOpacity_ == 255 ? MF_CHECKED : MF_UNCHECKED), kMenuOpacity100, L"透明度: 100%");
        AppendMenuW(displayMenu, MF_STRING | (windowOpacity_ == 230 ? MF_CHECKED : MF_UNCHECKED), kMenuOpacity90, L"透明度: 90%");
        AppendMenuW(displayMenu, MF_STRING | (windowOpacity_ == 205 ? MF_CHECKED : MF_UNCHECKED), kMenuOpacity80, L"透明度: 80%");
        AppendMenuW(displayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(displayMenu, MF_STRING, kMenuResetPosition, L"重置窗口位置");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displayMenu), L"显示与窗口");

        HMENU alertMenu = CreatePopupMenu();
        appendInfoItem(alertMenu, L"提醒阈值 " + alertThresholdText());
        AppendMenuW(alertMenu, MF_STRING | (highUsageAlerts_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleHighUsageAlerts, L"高占用提醒");
        AppendMenuW(alertMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(alertMenu, MF_STRING | (highUsageAlertThreshold_ == 80 ? MF_CHECKED : MF_UNCHECKED), kMenuAlertThreshold80, L"80%");
        AppendMenuW(alertMenu, MF_STRING | (highUsageAlertThreshold_ == 90 ? MF_CHECKED : MF_UNCHECKED), kMenuAlertThreshold90, L"90%");
        AppendMenuW(alertMenu, MF_STRING | (highUsageAlertThreshold_ == 95 ? MF_CHECKED : MF_UNCHECKED), kMenuAlertThreshold95, L"95%");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(alertMenu), L"提醒");

        HMENU toolMenu = CreatePopupMenu();
        AppendMenuW(toolMenu, MF_STRING, kMenuOpenTaskManager, L"打开任务管理器");
        AppendMenuW(toolMenu, MF_STRING, kMenuOpenResourceMonitor, L"打开资源监视器");
        AppendMenuW(toolMenu, MF_STRING, kMenuOpenAppFolder, L"打开程序目录");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(toolMenu), L"系统工具");

        HMENU startupMenu = CreatePopupMenu();
        AppendMenuW(startupMenu, MF_STRING | (startHidden_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleStartHidden, L"启动时隐藏面板");
        AppendMenuW(startupMenu, MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED), kMenuToggleAutoStart, L"开机自启");
        AppendMenuW(startupMenu, MF_STRING | (globalHotkeyRegistered_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggleGlobalHotkey, L"全局快捷键 Ctrl+Shift+M");
        AppendMenuW(startupMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(startupMenu, MF_STRING, kMenuResetSettings, L"恢复默认设置");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(startupMenu), L"启动与偏好");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");
    }

    void appendInfoItem(HMENU menu, const std::wstring& text) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text.c_str());
    }

    void populateDiskMenu(HMENU menu) {
        DWORD drives = GetLogicalDrives();
        if (drives == 0) {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"未发现可用盘符");
            return;
        }

        for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
            if ((drives & (1u << (letter - L'A'))) == 0) {
                continue;
            }
            std::wstring root;
            root.push_back(letter);
            root += L":\\";
            UINT type = GetDriveTypeW(root.c_str());
            if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_CDROM) {
                continue;
            }

            std::wstring label;
            label.push_back(letter);
            label += L":";
            UINT flags = MF_STRING | (letter == diskDriveLetter_ ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(menu, flags, kMenuDiskBase + static_cast<UINT>(letter - L'A'), label.c_str());
        }
    }

    std::wstring windowStateText() {
        std::wstring text = IsWindowVisible(hwnd_) ? L"状态: 面板显示中" : L"状态: 后台运行中";
        if (paused_) {
            text += L" / 已暂停";
        }
        if (alwaysOnTop_) {
            text += L" / 已置顶";
        }
        if (lockPosition_) {
            text += L" / 已锁定";
        }
        if (highUsageAlerts_) {
            text += L" / 高占用提醒";
        }
        if (globalHotkeyRegistered_) {
            text += L" / 快捷键";
        }
        if (backgroundEcoMode_ && !IsWindowVisible(hwnd_)) {
            text += L" / 低频刷新";
        }
        return text;
    }

    void showTrayBalloon(const std::wstring& title, const std::wstring& message) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        wcscpy_s(nid.szInfoTitle, title.c_str());
        wcscpy_s(nid.szInfo, message.c_str());
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    std::wstring statusSummaryText() {
        std::wstring text = L"MiniMonitor 当前状态\r\n";
        text += L"CPU: " + formatPercent(metrics_.cpu);
        if (!metrics_.topCpu.empty()) {
            text += L"  Top: " + metrics_.topCpu.front().name + L" " + formatOneDecimalPercent(metrics_.topCpu.front().cpu);
        }
        text += L"\r\nGPU: " + (metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A");
        if (!metrics_.topGpu.empty()) {
            text += L"  Top: " + metrics_.topGpu.front().name + L" " + formatOneDecimalPercent(metrics_.topGpu.front().gpu);
        }
        text += L"\r\nMemory: " + formatPercent(metrics_.memory) + L"  " +
                formatBytes(static_cast<double>(metrics_.memoryUsed)) + L" / " +
                formatBytes(static_cast<double>(metrics_.memoryTotal));
        if (!metrics_.topMemory.empty()) {
            text += L"  Top: " + metrics_.topMemory.front().name + L" " +
                    formatBytes(static_cast<double>(metrics_.topMemory.front().memory));
        }
        text += L"\r\nNetwork: Down " + formatSpeed(metrics_.netDown) + L"  Up " + formatSpeed(metrics_.netUp);
        text += L"\r\nDisk " + diskDisplayName() + L": Read " + (metrics_.diskRead >= 0 ? formatSpeed(metrics_.diskRead) : L"N/A") +
                L"  Write " + (metrics_.diskWrite >= 0 ? formatSpeed(metrics_.diskWrite) : L"N/A");
        text += L"\r\n" + trayQuotaText();
        return text;
    }

    std::wstring processRowsReport(const std::wstring& title, const std::vector<ProcessRow>& rows, const std::wstring& mode) {
        std::wstring text = title + L"\r\n";
        if (rows.empty()) {
            return text + L"  N/A\r\n";
        }
        for (const auto& row : rows) {
            text += L"  " + row.name + L"  PID " + std::to_wstring(row.pid);
            if (mode == L"cpu") {
                text += L"  CPU " + formatOneDecimalPercent(row.cpu);
            } else if (mode == L"memory") {
                text += L"  MEM " + formatBytes(static_cast<double>(row.memory));
            } else {
                text += L"  GPU " + (row.gpu >= 0.0 ? formatOneDecimalPercent(row.gpu) : L"N/A");
            }
            text += L"\r\n";
        }
        return text;
    }

    std::wstring statusReportText() {
        std::wstring text = statusSummaryText();
        text += L"\r\n\r\nTop Processes\r\n";
        text += processRowsReport(L"CPU", metrics_.topCpu, L"cpu");
        text += processRowsReport(L"Memory", metrics_.topMemory, L"memory");
        text += processRowsReport(L"GPU", metrics_.topGpu, L"gpu");

        text += L"\r\nCodex\r\n";
        text += L"  Status: " + metrics_.quota.status + L"\r\n";
        text += L"  " + metrics_.quota.firstLabel + L": " + metrics_.quota.firstUsage + L" / " +
                metrics_.quota.fiveHourReset + L"\r\n";
        text += L"  " + metrics_.quota.secondLabel + L": " + metrics_.quota.secondUsage + L" / " +
                metrics_.quota.sevenDayReset + L"\r\n";
        text += L"  Last updated: " + metrics_.quota.lastUpdated + L"\r\n";

        text += L"\r\nSettings\r\n";
        text += L"  Theme: " + themeName() + L"\r\n";
        text += L"  Disk: " + diskDisplayName() + L"\r\n";
        text += L"  Refresh interval: " + refreshIntervalText() + L"\r\n";
        text += L"  Background refresh: " + backgroundRefreshText() + L"\r\n";
        text += L"  Opacity: " + opacityText() + L"\r\n";
        text += L"  High usage alerts: " + std::wstring(highUsageAlerts_ ? L"On" : L"Off") +
                L" (" + alertThresholdText() + L")\r\n";
        text += L"  Always on top: " + std::wstring(alwaysOnTop_ ? L"On" : L"Off") + L"\r\n";
        text += L"  Position locked: " + std::wstring(lockPosition_ ? L"On" : L"Off") + L"\r\n";
        text += L"  Global hotkey: " + std::wstring(globalHotkeyRegistered_ ? L"Registered" : L"Off") + L"\r\n";
        text += L"\r\nGenerated: " + formatCurrentClock() + L"\r\n";
        return text;
    }

    bool setClipboardText(const std::wstring& text) {
        if (!OpenClipboard(hwnd_)) {
            return false;
        }
        EmptyClipboard();
        const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) {
            CloseClipboard();
            return false;
        }
        void* locked = GlobalLock(memory);
        if (!locked) {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        CopyMemory(locked, text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }

    bool writeUtf16TextFile(const std::wstring& path, const std::wstring& text) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }

        const WORD bom = 0xFEFF;
        DWORD written = 0;
        bool ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
        if (ok && !text.empty()) {
            const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
            written = 0;
            ok = WriteFile(file, text.c_str(), bytes, &written, nullptr) && written == bytes;
        }
        CloseHandle(file);
        return ok;
    }

    void copyStatusToClipboard() {
        requestMetricsSample(false);
        if (setClipboardText(statusSummaryText())) {
            showTrayBalloon(L"MiniMonitor", L"当前状态已复制到剪贴板。");
        } else {
            showTrayBalloon(L"MiniMonitor", L"复制失败，剪贴板可能正被占用。");
        }
    }

    void exportStatusReport() {
        requestMetricsSample(false);

        std::wstring folder;
        if (!ensureReportsDirectory(folder)) {
            showTrayBalloon(L"MiniMonitor", L"无法定位报告目录。");
            return;
        }

        const std::wstring filename = L"MiniMonitor-report-" + reportTimestampForFile() + L".txt";
        const std::wstring path = folder + L"\\" + filename;
        if (!writeUtf16TextFile(path, statusReportText())) {
            showTrayBalloon(L"MiniMonitor", L"状态报告导出失败。");
            return;
        }

        ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        showTrayBalloon(L"MiniMonitor", L"状态报告已导出: " + filename);
    }

    void openReportsFolder() {
        std::wstring folder;
        if (!ensureReportsDirectory(folder)) {
            showTrayBalloon(L"MiniMonitor", L"无法定位报告目录。");
            return;
        }
        HINSTANCE result = ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            showTrayBalloon(L"MiniMonitor", L"无法打开报告目录。");
        }
    }

    void clearReports() {
        std::wstring folder;
        if (!ensureReportsDirectory(folder)) {
            showTrayBalloon(L"MiniMonitor", L"无法定位报告目录。");
            return;
        }

        const int choice = MessageBoxW(hwnd_,
                                       L"将删除报告目录中的 MiniMonitor-report-*.txt 文件。\n\n是否继续？",
                                       L"MiniMonitor", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice != IDYES) {
            return;
        }

        const std::wstring pattern = folder + L"\\MiniMonitor-report-*.txt";
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            showTrayBalloon(L"MiniMonitor", L"没有可清理的状态报告。");
            return;
        }

        int deleted = 0;
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                continue;
            }
            const std::wstring path = folder + L"\\" + data.cFileName;
            if (DeleteFileW(path.c_str())) {
                ++deleted;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);

        if (deleted == 0) {
            showTrayBalloon(L"MiniMonitor", L"没有可清理的状态报告。");
        } else {
            showTrayBalloon(L"MiniMonitor", L"已清理 " + std::to_wstring(deleted) + L" 份状态报告。");
        }
    }

    void refreshNow(bool showFeedback) {
        requestMetricsSample(showFeedback);
    }

    void checkHighUsageAlerts() {
        if (!highUsageAlerts_ || paused_) {
            return;
        }

        const double threshold = static_cast<double>(highUsageAlertThreshold_) / 100.0;
        const bool highCpu = metrics_.cpu >= threshold;
        const bool highMemory = metrics_.memory >= threshold;
        if (!highCpu && !highMemory) {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (lastHighUsageAlertTick_ != 0 && now - lastHighUsageAlertTick_ < kHighUsageAlertCooldownMs) {
            return;
        }
        lastHighUsageAlertTick_ = now;

        std::wstring message;
        if (highCpu) {
            message += L"CPU " + formatPercent(metrics_.cpu);
            if (!metrics_.topCpu.empty()) {
                message += L" · " + metrics_.topCpu.front().name + L" " + formatOneDecimalPercent(metrics_.topCpu.front().cpu);
            }
        }
        if (highMemory) {
            if (!message.empty()) {
                message += L"\n";
            }
            message += L"内存 " + formatPercent(metrics_.memory);
            if (!metrics_.topMemory.empty()) {
                message += L" · " + metrics_.topMemory.front().name + L" " +
                           formatBytes(static_cast<double>(metrics_.topMemory.front().memory));
            }
        }
        showTrayBalloon(L"MiniMonitor 高占用提醒", message);
    }

    void setRefreshInterval(UINT intervalMs) {
        intervalMs = sanitizeRefreshInterval(intervalMs);
        refreshIntervalMs_ = intervalMs;
        writeDwordSetting(L"RefreshIntervalMs", refreshIntervalMs_);
        applyRefreshTimer();
        showTrayBalloon(L"MiniMonitor", L"刷新间隔已设置为 " + refreshIntervalText() + L"。");
    }

    UINT effectiveRefreshInterval() {
        if (backgroundEcoMode_ && hwnd_ && !IsWindowVisible(hwnd_)) {
            return kBackgroundEcoRefreshIntervalMs;
        }
        return refreshIntervalMs_;
    }

    void applyRefreshTimer() {
        if (!hwnd_) {
            return;
        }
        SetTimer(hwnd_, kRefreshTimer, effectiveRefreshInterval(), nullptr);
    }

    std::wstring refreshIntervalText() {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%u 秒", refreshIntervalMs_ / 1000);
        return buffer;
    }

    std::wstring backgroundRefreshText() {
        if (!backgroundEcoMode_) {
            return L"跟随前台";
        }
        return IsWindowVisible(hwnd_) ? L"跟随前台" : L"10 秒";
    }

    std::wstring opacityText() {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%u%%", static_cast<unsigned int>((static_cast<unsigned int>(windowOpacity_) * 100 + 127) / 255));
        return buffer;
    }

    std::wstring alertThresholdText() {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"%lu%%", highUsageAlertThreshold_);
        return buffer;
    }

    void setHighUsageAlertThreshold(DWORD threshold) {
        highUsageAlertThreshold_ = sanitizeAlertThreshold(threshold);
        writeDwordSetting(L"HighUsageAlertThreshold", highUsageAlertThreshold_);
        lastHighUsageAlertTick_ = 0;
        showTrayBalloon(L"MiniMonitor", L"高占用提醒阈值已设置为 " + alertThresholdText() + L"。");
    }

    void openTaskManager() {
        HINSTANCE result = ShellExecuteW(hwnd_, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            showTrayBalloon(L"MiniMonitor", L"无法打开任务管理器。");
        }
    }

    void openResourceMonitor() {
        HINSTANCE result = ShellExecuteW(hwnd_, L"open", L"resmon.exe", nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            showTrayBalloon(L"MiniMonitor", L"无法打开资源监视器。");
        }
    }

    void openAppFolder() {
        const std::wstring folder = appDirectory();
        if (folder.empty()) {
            showTrayBalloon(L"MiniMonitor", L"无法定位程序目录。");
            return;
        }
        HINSTANCE result = ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            showTrayBalloon(L"MiniMonitor", L"无法打开程序目录。");
        }
    }

    void resetAppSettings() {
        const int choice = MessageBoxW(hwnd_,
                                       L"恢复默认设置会重置窗口位置、透明度、刷新频率、提醒、快捷键等 MiniMonitor 设置。\n\n是否继续？",
                                       L"MiniMonitor", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice != IDYES) {
            return;
        }

        settings_.clear();
        startHidden_ = false;
        alwaysOnTop_ = false;
        paused_ = false;
        lockPosition_ = false;
        highUsageAlerts_ = false;
        globalHotkeyEnabled_ = true;
        backgroundEcoMode_ = false;
        theme_ = UiTheme::Mono;
        diskDriveLetter_ = systemDriveLetter();
        highUsageAlertThreshold_ = kDefaultHighUsageAlertThreshold;
        refreshIntervalMs_ = kDefaultRefreshIntervalMs;
        windowOpacity_ = 255;
        lastHighUsageAlertTick_ = 0;

        applyAlwaysOnTop();
        applyWindowOpacity();
        applyGlobalHotkey(false);
        applyRefreshTimer();
        movePanelToDefaultPosition(false);
        requestMetricsSample(false);
        updateTrayTip();
        InvalidateRect(hwnd_, nullptr, FALSE);
        showTrayBalloon(L"MiniMonitor", L"已恢复默认设置。");
    }

    bool applyGlobalHotkey(bool announce) {
        unregisterGlobalHotkey();
        if (!globalHotkeyEnabled_ || !hwnd_) {
            return false;
        }

        globalHotkeyRegistered_ = RegisterHotKey(hwnd_, kHotkeyTogglePanel, MOD_CONTROL | MOD_SHIFT, L'M') != FALSE;
        if (announce) {
            showTrayBalloon(L"MiniMonitor", globalHotkeyRegistered_ ? L"全局快捷键 Ctrl+Shift+M 已开启。" : L"全局快捷键注册失败，可能已被其他程序占用。");
        }
        return globalHotkeyRegistered_;
    }

    void unregisterGlobalHotkey() {
        if (globalHotkeyRegistered_ && hwnd_) {
            UnregisterHotKey(hwnd_, kHotkeyTogglePanel);
        }
        globalHotkeyRegistered_ = false;
    }

    void toggleGlobalHotkey() {
        if (globalHotkeyRegistered_) {
            unregisterGlobalHotkey();
            globalHotkeyEnabled_ = false;
            writeBoolSetting(L"GlobalHotkeyEnabled", false);
            showTrayBalloon(L"MiniMonitor", L"全局快捷键已关闭。");
            return;
        }

        globalHotkeyEnabled_ = true;
        if (applyGlobalHotkey(true)) {
            writeBoolSetting(L"GlobalHotkeyEnabled", true);
        } else {
            globalHotkeyEnabled_ = false;
        }
    }

    void toggleBackgroundEcoMode() {
        backgroundEcoMode_ = !backgroundEcoMode_;
        writeBoolSetting(L"BackgroundEcoMode", backgroundEcoMode_);
        applyRefreshTimer();
        showTrayBalloon(L"MiniMonitor", backgroundEcoMode_ ? L"后台低频刷新已开启，隐藏时每 10 秒更新一次。" : L"后台低频刷新已关闭。");
    }

    void applyWindowOpacity() {
        if (!hwnd_) {
            return;
        }
        SetLayeredWindowAttributes(hwnd_, 0, windowOpacity_, LWA_ALPHA);
    }

    void setWindowOpacity(BYTE opacity) {
        windowOpacity_ = sanitizeWindowOpacity(opacity);
        writeDwordSetting(L"WindowOpacity", windowOpacity_);
        applyWindowOpacity();
        showTrayBalloon(L"MiniMonitor", L"窗口透明度已设置为 " + opacityText() + L"。");
    }

    void setTheme(UiTheme theme) {
        theme_ = theme;
        writeDwordSetting(L"Theme", static_cast<DWORD>(theme_));
        updateTrayTip();
        InvalidateRect(hwnd_, nullptr, TRUE);
        showTrayBalloon(L"MiniMonitor", L"主题已切换为 " + themeName() + L"。");
    }

    void setDiskDrive(wchar_t letter) {
        diskDriveLetter_ = sanitizeDiskDrive(static_cast<DWORD>(letter));
        writeDwordSetting(L"DiskDrive", static_cast<DWORD>(diskDriveLetter_));
        requestMetricsSample(false);
        showTrayBalloon(L"MiniMonitor", L"磁盘监控已切换到 " + diskDisplayName() + L"。");
    }

    void toggleLockPosition() {
        lockPosition_ = !lockPosition_;
        writeBoolSetting(L"LockPosition", lockPosition_);
        showTrayBalloon(L"MiniMonitor", lockPosition_ ? L"窗口位置已锁定，标题区不会拖动。" : L"窗口位置锁定已关闭。");
    }

    void toggleHighUsageAlerts() {
        highUsageAlerts_ = !highUsageAlerts_;
        writeBoolSetting(L"HighUsageAlerts", highUsageAlerts_);
        if (highUsageAlerts_) {
            lastHighUsageAlertTick_ = 0;
        }
        showTrayBalloon(L"MiniMonitor", highUsageAlerts_ ? L"高占用提醒已开启，CPU 或内存超过 " + alertThresholdText() + L" 时提醒。" : L"高占用提醒已关闭。");
    }

    void toggleStartHidden() {
        startHidden_ = !startHidden_;
        writeBoolSetting(L"StartHidden", startHidden_);
        showTrayBalloon(L"MiniMonitor", startHidden_ ? L"下次启动将直接进入托盘。" : L"下次启动将显示面板。");
    }

    void toggleAutoStart() {
        const bool enabled = !isAutoStartEnabled();
        if (setAutoStart(enabled)) {
            showTrayBalloon(L"MiniMonitor", enabled ? L"已开启开机自启。" : L"已关闭开机自启。");
        } else {
            showTrayBalloon(L"MiniMonitor", L"开机自启设置失败。");
        }
    }

    void applyAlwaysOnTop() {
        if (!hwnd_) {
            return;
        }
        SetWindowPos(hwnd_, alwaysOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void toggleAlwaysOnTop() {
        alwaysOnTop_ = !alwaysOnTop_;
        writeBoolSetting(L"AlwaysOnTop", alwaysOnTop_);
        applyAlwaysOnTop();
        showTrayBalloon(L"MiniMonitor", alwaysOnTop_ ? L"窗口将保持在其他窗口上方。" : L"窗口置顶已关闭。");
    }

    void togglePause() {
        paused_ = !paused_;
        writeBoolSetting(L"Paused", paused_);
        updateTrayTip();
        InvalidateRect(hwnd_, nullptr, FALSE);
        showTrayBalloon(L"MiniMonitor", paused_ ? L"已暂停自动刷新。" : L"已恢复自动刷新。");
    }

    void resetWindowPosition() {
        movePanelToDefaultPosition(true);
        showPanel();
        showTrayBalloon(L"MiniMonitor", L"窗口已回到屏幕右侧。");
    }

    void movePanelToDefaultPosition(bool savePosition) {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        const int width = scalePx(kPanelWidth);
        const int height = std::max(scalePx(560), static_cast<int>(rect.bottom - rect.top));
        const int x = workArea.right - width - scalePx(16);
        const int y = workArea.top + scalePx(16);
        MoveWindow(hwnd_, x, y, width, height, TRUE);
        if (savePosition) {
            saveWindowPosition();
        }
    }

    std::wstring trayQuotaText() {
        if (!metrics_.quota.available) {
            return L"Codex: " + metrics_.quota.status;
        }
        return L"Codex: 5h " + metrics_.quota.firstUsage + L"    Weekly " + metrics_.quota.secondUsage;
    }

    std::wstring trayTopProcessText() {
        if (metrics_.topCpu.empty()) {
            return L"Top CPU: N/A";
        }
        const auto& top = metrics_.topCpu.front();
        return L"Top CPU: " + top.name + L" " + formatOneDecimalPercent(top.cpu);
    }

    std::wstring trayTopMemoryText() {
        if (metrics_.topMemory.empty()) {
            return L"Top Memory: N/A";
        }
        const auto& top = metrics_.topMemory.front();
        return L"Top Memory: " + top.name + L" " + formatBytes(static_cast<double>(top.memory));
    }

    std::wstring trayTopGpuText() {
        if (metrics_.topGpu.empty()) {
            return L"Top GPU: N/A";
        }
        const auto& top = metrics_.topGpu.front();
        return L"Top GPU: " + top.name + L" " + (top.gpu >= 0.0 ? formatOneDecimalPercent(top.gpu) : L"N/A");
    }

    std::wstring compactProcessName(const std::wstring& name, size_t maxChars) {
        if (name.size() <= maxChars) {
            return name;
        }
        if (maxChars <= 3) {
            return name.substr(0, maxChars);
        }
        return name.substr(0, maxChars - 3) + L"...";
    }

    std::wstring trayTipText() {
        std::wstring tip;
        if (paused_) {
            tip += L"Paused\n";
        }
        tip += L"CPU " + formatPercent(metrics_.cpu) +
                           L"  GPU " + (metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A") +
                           L"\nMEM " + formatPercent(metrics_.memory) +
                           L"  NET ↓" + formatSpeed(metrics_.netDown) +
                           L" ↑" + formatSpeed(metrics_.netUp);
        if (metrics_.quota.available) {
            tip += L"\nCodex 5h " + metrics_.quota.firstUsage + L"  Weekly " + metrics_.quota.secondUsage;
        } else {
            tip += L"\nCodex " + metrics_.quota.status;
        }
        if (!metrics_.topCpu.empty()) {
            tip += L"\nTop CPU " + compactProcessName(metrics_.topCpu.front().name, 18) + L" " +
                   formatOneDecimalPercent(metrics_.topCpu.front().cpu);
        }
        if (!metrics_.topMemory.empty()) {
            tip += L"  MEM " + compactProcessName(metrics_.topMemory.front().name, 12);
        }
        return trimForTip(tip);
    }

    LRESULT hitTest(LPARAM lParam) {
        if (lockPosition_) {
            return HTCLIENT;
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int logicalX = unscalePx(point.x);
        const int logicalY = unscalePx(point.y);
        if (logicalY >= 0 && logicalY < 92 && logicalX >= 0 && logicalX < unscalePx(client.right)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    void handleClick(int x, int y) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        x = unscalePx(x);
        y = unscalePx(y);
        const int logicalWidth = unscalePx(client.right);
        const int logicalHeight = unscalePx(client.bottom);
        if (containsPoint(quotaRefreshButtonRect(logicalWidth), x, y)) {
            refreshCodexQuotaNow();
            return;
        }

        const int bottom = logicalHeight - 42;
        if (y < bottom) {
            return;
        }
        if (x >= logicalWidth - 62) {
            DestroyWindow(hwnd_);
        } else if (x >= logicalWidth - 116) {
            MessageBoxW(hwnd_, L"MiniMonitor\n2 秒刷新，托盘常驻。", L"MiniMonitor", MB_OK | MB_ICONINFORMATION);
        } else if (x >= logicalWidth - 170) {
            hidePanel();
        }
    }

    bool containsPoint(RectF rect, int x, int y) {
        return static_cast<REAL>(x) >= rect.X && static_cast<REAL>(x) <= rect.X + rect.Width &&
               static_cast<REAL>(y) >= rect.Y && static_cast<REAL>(y) <= rect.Y + rect.Height;
    }

    RectF quotaRefreshButtonRect(int width) {
        const REAL margin = 18.0f;
        const REAL top = 96.0f;
        const REAL quotaY = top + 260.0f;
        return RectF(static_cast<REAL>(width) - margin - 118.0f, quotaY + 130.0f, 100.0f, 24.0f);
    }

    void refreshCodexQuotaNow() {
        startCodexQuotaRefresh(true, true);
    }

    void startCodexQuotaRefresh(bool forceRefresh, bool announceIfBusy) {
        if (quotaRefreshInProgress_) {
            if (announceIfBusy) {
                showTrayBalloon(L"MiniMonitor", L"Codex 额度正在刷新，请稍等。");
            }
            return;
        }

        quotaRefreshInProgress_ = true;
        metrics_.quota.checked = true;
        metrics_.quota.available = false;
        metrics_.quota.status = L"Refreshing";
        metrics_.quota.firstUsage = L"...";
        metrics_.quota.secondUsage = L"...";
        metrics_.quota.firstProgress = 0.0;
        metrics_.quota.secondProgress = 0.0;
        metrics_.quota.lastUpdated = L"Updating...";
        updateTrayTip();
        InvalidateRect(hwnd_, nullptr, FALSE);

        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (workersStopping_) {
                quotaRefreshInProgress_ = false;
                return;
            }
            quotaRequested_ = true;
            quotaForceRequested_ = quotaForceRequested_ || forceRefresh;
            quotaAnnounceRequested_ = quotaAnnounceRequested_ || forceRefresh;
        }
        quotaCv_.notify_one();
    }

    void handleQuotaRefreshComplete() {
        std::unique_ptr<CodexQuota> quota;
        bool announce = false;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            quota = std::move(readyQuota_);
            announce = readyQuotaAnnounce_;
            readyQuotaAnnounce_ = false;
        }
        if (!quota) {
            return;
        }

        quotaRefreshInProgress_ = false;
        metrics_.quota = *quota;
        InvalidateRect(hwnd_, nullptr, FALSE);
        updateTrayTip();

        if (announce) {
            if (metrics_.quota.available) {
                showTrayBalloon(L"Codex 额度已更新",
                                metrics_.quota.firstLabel + L" " + metrics_.quota.firstUsage + L" / " +
                                    metrics_.quota.fiveHourReset + L"\n" +
                                    metrics_.quota.secondLabel + L" " + metrics_.quota.secondUsage + L" / " +
                                    metrics_.quota.sevenDayReset);
            } else {
                showTrayBalloon(L"Codex 额度刷新失败",
                                metrics_.quota.status + L"。请确认 Codex/ChatGPT 已登录后再刷新。");
            }
        }
    }

    Font makeFont(REAL size, INT style = FontStyleRegular) {
        FontFamily family(L"Segoe UI Variable");
        if (!family.IsAvailable()) {
            return Font(L"Segoe UI", size, style, UnitPixel);
        }
        return Font(&family, size, style, UnitPixel);
    }

    void drawText(Graphics& g, const std::wstring& text, RectF rect, Font& font, Color color,
                  StringAlignment horizontal = StringAlignmentNear,
                  StringAlignment vertical = StringAlignmentNear) {
        SolidBrush brush(color);
        StringFormat format;
        format.SetAlignment(horizontal);
        format.SetLineAlignment(vertical);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int logicalWidth = unscalePx(width);
        const int logicalHeight = unscalePx(height);

        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        auto oldBitmap = SelectObject(memoryDc, bitmap);

        Graphics g(memoryDc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.ScaleTransform(dpiScale(), dpiScale());
        drawBackground(g, logicalWidth, logicalHeight);
        drawHeader(g, logicalWidth);
        drawCards(g, logicalWidth, logicalHeight);
        drawWindowFrame(g, logicalWidth, logicalHeight);

        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        EndPaint(hwnd_, &ps);
    }

    void drawBackground(Graphics& g, int width, int height) {
        const auto t = theme();
        LinearGradientBrush bg(
            Rect(0, 0, width, height),
            t.bgTop,
            t.bgBottom,
            LinearGradientModeVertical);
        g.FillRectangle(&bg, 0, 0, width, height);
    }

    void drawWindowFrame(Graphics& g, int width, int height) {
        const auto t = theme();
        RectF outer(0.5f, 0.5f, static_cast<REAL>(width) - 1.0f, static_cast<REAL>(height) - 1.0f);
        auto outerPath = roundedRect(outer, 12.0f);
        Pen outerPen(t.frameLight, 1.0f);
        g.DrawPath(&outerPen, outerPath.get());

        RectF inner(1.5f, 1.5f, static_cast<REAL>(width) - 3.0f, static_cast<REAL>(height) - 3.0f);
        auto innerPath = roundedRect(inner, 11.0f);
        Pen innerPen(t.frameDark, 1.0f);
        g.DrawPath(&innerPen, innerPath.get());
    }

    void drawHeader(Graphics& g, int) {
        const auto t = theme();
        Font title = makeFont(22, FontStyleBold);
        Font subtitle = makeFont(13, FontStyleBold);

        RectF iconRect(28, 24, 48, 48);
        auto iconPath = roundedRect(iconRect, 12.0f);
        SolidBrush iconBrush(t.iconBg);
        g.FillPath(&iconBrush, iconPath.get());
        Pen iconPen(t.iconFg, 2.0f);
        g.DrawEllipse(&iconPen, iconRect.X + 12.0f, iconRect.Y + 12.0f, 24.0f, 24.0f);
        g.DrawLine(&iconPen, iconRect.X + 24, iconRect.Y + 18, iconRect.X + 24, iconRect.Y + 31);
        g.DrawLine(&iconPen, iconRect.X + 24, iconRect.Y + 31, iconRect.X + 32, iconRect.Y + 25);

        drawText(g, L"MiniMonitor", RectF(90, 24, 240, 28), title, t.text);
        drawText(g, machineName_, RectF(91, 53, 260, 22), subtitle, t.muted);

        if (paused_) {
            Font pausedFont = makeFont(11, FontStyleBold);
            RectF badge(330, 30, 72, 24);
            auto badgePath = roundedRect(badge, 7.0f);
            SolidBrush badgeFill(t.iconBg);
            g.FillPath(&badgeFill, badgePath.get());
            drawText(g, L"PAUSED", badge, pausedFont, t.iconFg, StringAlignmentCenter, StringAlignmentCenter);
        }
    }

    void drawCards(Graphics& g, int width, int height) {
        const auto t = theme();
        const REAL margin = 18.0f;
        const REAL gap = 12.0f;
        const REAL top = 96.0f;
        const REAL halfW = (width - margin * 2.0f - gap) / 2.0f;

        drawMetricCard(g, RectF(margin, top, halfW, 132), L"CPU", formatPercent(metrics_.cpu),
                       L"系统负载", cpuHistory_, t.accent, CardIcon::Cpu);
        drawGpuCard(g, RectF(margin + halfW + gap, top, halfW, 132));
        drawMemoryCard(g, RectF(margin, top + 144, width - margin * 2.0f, 104));

        const REAL quotaY = top + 260;
        drawQuotaCard(g, RectF(margin, quotaY, width - margin * 2.0f, 164), width);

        const REAL appsY = quotaY + 176;
        drawTopAppsCard(g, RectF(margin, appsY, width - margin * 2.0f, 116));

        const REAL rowY = appsY + 128;
        const REAL footerY = static_cast<REAL>(height - 48);
        const REAL smallCardHeight = std::clamp(footerY - rowY - 8.0f, 64.0f, 76.0f);
        drawSmallStatCard(g, RectF(margin, rowY, halfW, smallCardHeight), L"Network",
                          L"↓ " + formatSpeed(metrics_.netDown),
                          L"↑ " + formatSpeed(metrics_.netUp),
                          t.accent2, CardIcon::Network);
        drawSmallStatCard(g, RectF(margin + halfW + gap, rowY, halfW, smallCardHeight), L"Disk " + diskDisplayName(),
                          formatBytes(static_cast<double>(metrics_.diskUsed)) + L" / " +
                              formatBytes(static_cast<double>(metrics_.diskTotal)),
                          L"Read " + (metrics_.diskRead >= 0 ? formatSpeed(metrics_.diskRead) : L"N/A"),
                          t.accent3, CardIcon::Disk);
        drawFooter(g, width, height);
    }

    void drawPanel(Graphics& g, RectF rect) {
        const auto t = theme();
        RectF shadowRect(rect.X, rect.Y + 2.0f, rect.Width, rect.Height);
        auto shadowPath = roundedRect(shadowRect, 8.0f);
        SolidBrush shadow(Color(22, 0, 0, 0));
        g.FillPath(&shadow, shadowPath.get());

        auto path = roundedRect(rect, 8.0f);
        SolidBrush fill(t.panel);
        Pen border(t.panelBorder, 1.0f);
        Pen edge(t.panelEdge, 1.0f);
        g.FillPath(&fill, path.get());
        g.DrawPath(&border, path.get());
        RectF inset(rect.X + 0.5f, rect.Y + 0.5f, rect.Width - 1.0f, rect.Height - 1.0f);
        auto insetPath = roundedRect(inset, 7.5f);
        g.DrawPath(&edge, insetPath.get());
    }

    void drawCardIcon(Graphics& g, RectF rect, CardIcon icon) {
        const auto t = theme();
        Pen pen(t.accent, 1.8f);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        Pen lightPen(t.subtle, 1.3f);
        SolidBrush dot(t.accent);
        const REAL x = rect.X;
        const REAL y = rect.Y;
        const REAL w = rect.Width;
        const REAL h = rect.Height;
        const REAL cx = x + w / 2.0f;
        const REAL cy = y + h / 2.0f;

        switch (icon) {
        case CardIcon::Cpu:
            g.DrawRectangle(&pen, x + 6, y + 6, w - 12, h - 12);
            g.DrawRectangle(&lightPen, x + 9, y + 9, w - 18, h - 18);
            for (int i = 0; i < 3; ++i) {
                const REAL p = y + 7.0f + i * 5.0f;
                g.DrawLine(&lightPen, x + 2, p, x + 6, p);
                g.DrawLine(&lightPen, x + w - 6, p, x + w - 2, p);
                g.DrawLine(&lightPen, p, y + 2, p, y + 6);
                g.DrawLine(&lightPen, p, y + h - 6, p, y + h - 2);
            }
            break;
        case CardIcon::Gpu:
            g.DrawRectangle(&pen, x + 4, y + 5, w - 8, h - 11);
            g.DrawLine(&pen, cx, y + h - 6, cx, y + h - 3);
            g.DrawLine(&pen, cx - 6, y + h - 3, cx + 6, y + h - 3);
            break;
        case CardIcon::Memory:
            g.DrawRectangle(&pen, x + 3, y + 8, w - 6, h - 12);
            for (int i = 0; i < 4; ++i) {
                const REAL px = x + 6.0f + i * 4.0f;
                g.DrawLine(&lightPen, px, y + 4, px, y + 8);
                g.DrawLine(&lightPen, px, y + h - 4, px, y + h - 8);
            }
            break;
        case CardIcon::Codex:
            g.DrawRectangle(&pen, x + 4, y + 5, w - 8, h - 10);
            g.DrawLine(&pen, x + 8, y + 10, x + 12, y + 13);
            g.DrawLine(&pen, x + 12, y + 13, x + 8, y + 16);
            g.DrawLine(&lightPen, x + 15, y + 16, x + 20, y + 16);
            break;
        case CardIcon::Apps:
            for (int r = 0; r < 2; ++r) {
                for (int c = 0; c < 2; ++c) {
                    g.DrawRectangle(&pen, x + 4.0f + c * 10.0f, y + 4.0f + r * 10.0f, 6.0f, 6.0f);
                }
            }
            break;
        case CardIcon::Network:
            g.DrawLine(&pen, cx, y + 3, cx, y + h - 3);
            g.DrawLine(&pen, cx, y + 3, cx - 5, y + 8);
            g.DrawLine(&pen, cx, y + 3, cx + 5, y + 8);
            g.DrawLine(&pen, cx, y + h - 3, cx - 5, y + h - 8);
            g.DrawLine(&pen, cx, y + h - 3, cx + 5, y + h - 8);
            break;
        case CardIcon::Disk:
            g.DrawEllipse(&pen, x + 5, y + 4, w - 10, h - 8);
            g.DrawArc(&lightPen, x + 7, y + 7, w - 14, h - 14, 25, 300);
            g.FillEllipse(&dot, cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
            break;
        }
    }

    void drawMetricCard(Graphics& g, RectF rect, const std::wstring& title, const std::wstring& value,
                        const std::wstring& subtitle, const SampleHistory& history, Color accent, CardIcon icon) {
        const auto t = theme();
        drawPanel(g, rect);

        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(28, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawCardIcon(g, RectF(rect.X + rect.Width - 42, rect.Y + 16, 24, 24), icon);
        drawText(g, title, RectF(rect.X + 18, rect.Y + 18, rect.Width - 128, 24), label, t.text);
        drawText(g, value, RectF(rect.X + rect.Width - 134, rect.Y + 15, 86, 38), valueFont, t.text,
                 StringAlignmentFar);
        drawText(g, subtitle, RectF(rect.X + 18, rect.Y + 47, rect.Width - 36, 20), small, t.muted);
        drawSparkline(g, RectF(rect.X + 18, rect.Y + 78, rect.Width - 36, 46), history, accent);
        drawLegend(g, rect.X + 18, rect.Y + rect.Height - 24, L"2s refresh", accent);
    }

    void drawGpuCard(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(26, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawCardIcon(g, RectF(rect.X + rect.Width - 42, rect.Y + 16, 24, 24), CardIcon::Gpu);
        drawText(g, L"GPU", RectF(rect.X + 18, rect.Y + 18, rect.Width - 128, 24), label, t.text);
        drawText(g, metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A",
                 RectF(rect.X + rect.Width - 134, rect.Y + 16, 86, 34), valueFont, t.text,
                 StringAlignmentFar);
        drawText(g, metrics_.gpuName, RectF(rect.X + 18, rect.Y + 52, rect.Width - 36, 36), small, t.muted);

        drawSparkline(g, RectF(rect.X + 18, rect.Y + 82, rect.Width - 36, 32), gpuHistory_, t.accent2);
        drawLegend(g, rect.X + 18, rect.Y + rect.Height - 24, L"GPU engine", t.accent2);
    }

    void drawMemoryCard(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(27, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawCardIcon(g, RectF(rect.X + rect.Width - 44, rect.Y + 16, 24, 24), CardIcon::Memory);
        drawText(g, L"Memory", RectF(rect.X + 20, rect.Y + 16, 170, 26), label, t.text);
        drawText(g, formatPercent(metrics_.memory), RectF(rect.X + rect.Width - 164, rect.Y + 12, 116, 36), valueFont,
                 t.text, StringAlignmentFar);

        RectF bar(rect.X + 20, rect.Y + 54, rect.Width - 40, 18);
        auto bgPath = roundedRect(bar, 6.0f);
        SolidBrush bg(t.barBg);
        g.FillPath(&bg, bgPath.get());
        RectF used = bar;
        used.Width *= static_cast<REAL>(metrics_.memory);
        auto usedPath = roundedRect(used, 6.0f);
        SolidBrush usedBrush(t.accent);
        g.FillPath(&usedBrush, usedPath.get());

        drawText(g,
                 formatBytes(static_cast<double>(metrics_.memoryUsed)) + L" / " +
                     formatBytes(static_cast<double>(metrics_.memoryTotal)),
                 RectF(rect.X + 20, rect.Y + 78, 170, 20), small, t.muted);
        drawSparkline(g, RectF(rect.X + rect.Width - 130, rect.Y + 76, 108, 18), memoryHistory_, t.accent);
    }

    void drawQuotaCard(Graphics& g, RectF rect, int width) {
        const auto t = theme();
        drawPanel(g, rect);
        drawCardIcon(g, RectF(rect.X + rect.Width - 42, rect.Y + 16, 24, 24), CardIcon::Codex);
        std::wstring firstDetail = metrics_.quota.available ? metrics_.quota.fiveHourReset : metrics_.quota.status;
        std::wstring secondDetail = metrics_.quota.available ? metrics_.quota.sevenDayReset : L"";
        drawQuotaLine(g, RectF(rect.X + 18, rect.Y + 13, rect.Width - 74, 48), false, metrics_.quota.firstLabel,
                      metrics_.quota.firstUsage, metrics_.quota.firstProgress, firstDetail);
        drawQuotaLine(g, RectF(rect.X + 18, rect.Y + 72, rect.Width - 36, 48), true, metrics_.quota.secondLabel,
                      metrics_.quota.secondUsage, metrics_.quota.secondProgress, secondDetail);
        Font stamp = makeFont(11, FontStyleRegular);
        drawText(g, metrics_.quota.lastUpdated, RectF(rect.X + 18, rect.Y + 134, rect.Width - 152, 18), stamp,
                 t.subtle, StringAlignmentFar);
        drawQuotaRefreshButton(g, quotaRefreshButtonRect(width));
    }

    void drawQuotaLine(Graphics& g, RectF rect, bool weekly, const std::wstring& window,
                       const std::wstring& usage, double progress, const std::wstring& reset) {
        const auto t = theme();
        Font label = makeFont(15, FontStyleRegular);
        Font percent = makeFont(15, FontStyleBold);
        Font detail = makeFont(12, FontStyleRegular);
        Color text = t.muted;
        Color ink = weekly ? t.accent2 : t.accent;

        drawQuotaIcon(g, rect.X, rect.Y + 1.0f, weekly, text);
        drawText(g, window, RectF(rect.X + 27, rect.Y - 1, 150, 22), label, text);
        drawText(g, usage, RectF(rect.X + rect.Width - 78, rect.Y - 1, 78, 22), percent, ink, StringAlignmentFar);

        RectF bar(rect.X, rect.Y + 27, rect.Width, 7);
        auto bgPath = roundedRect(bar, 3.5f);
        SolidBrush bg(t.barBg);
        g.FillPath(&bg, bgPath.get());
        if (progress > 0.0) {
            RectF fill = bar;
            fill.Width *= static_cast<REAL>(std::clamp(progress, 0.0, 1.0));
            auto fillPath = roundedRect(fill, 3.5f);
            SolidBrush fillBrush(ink);
            g.FillPath(&fillBrush, fillPath.get());
        }

        drawText(g, reset, RectF(rect.X + 116, rect.Y + 37, rect.Width - 116, 18), detail, text, StringAlignmentFar);
    }

    void drawQuotaIcon(Graphics& g, REAL x, REAL y, bool weekly, Color color) {
        Pen pen(color, 1.6f);
        if (!weekly) {
            g.DrawEllipse(&pen, x, y + 1.0f, 15.0f, 15.0f);
            g.DrawLine(&pen, x + 7.5f, y + 4.5f, x + 7.5f, y + 9.0f);
            g.DrawLine(&pen, x + 7.5f, y + 9.0f, x + 11.0f, y + 11.0f);
            return;
        }
        g.DrawRectangle(&pen, x + 1.0f, y + 3.0f, 14.0f, 13.0f);
        g.DrawLine(&pen, x + 1, y + 7, x + 15, y + 7);
        g.DrawLine(&pen, x + 5, y + 1, x + 5, y + 5);
        g.DrawLine(&pen, x + 11, y + 1, x + 11, y + 5);
    }

    void drawQuotaRefreshButton(Graphics& g, RectF rect) {
        const auto t = theme();
        auto path = roundedRect(rect, 7.0f);
        SolidBrush bg(t.buttonBg);
        g.FillPath(&bg, path.get());
        Pen border(t.panelEdge, 1.0f);
        g.DrawPath(&border, path.get());

        Pen icon(t.accent, 1.7f);
        icon.SetStartCap(LineCapRound);
        icon.SetEndCap(LineCapRound);
        const REAL cx = rect.X + 13.0f;
        const REAL cy = rect.Y + rect.Height / 2.0f;
        g.DrawArc(&icon, cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, 35.0f, 280.0f);
        g.DrawLine(&icon, cx + 4.5f, cy - 6.0f, cx + 8.0f, cy - 6.0f);
        g.DrawLine(&icon, cx + 8.0f, cy - 6.0f, cx + 8.0f, cy - 2.5f);

        Font font = makeFont(12, FontStyleBold);
        drawText(g, L"Refresh", RectF(rect.X + 28, rect.Y + 3, rect.Width - 32, 18), font,
                 t.text);
    }

    void drawTopAppsCard(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font title = makeFont(16, FontStyleBold);
        Font row = makeFont(12, FontStyleBold);

        drawCardIcon(g, RectF(rect.X + rect.Width - 42, rect.Y + 12, 24, 24), CardIcon::Apps);
        drawText(g, L"Top Apps", RectF(rect.X + 18, rect.Y + 12, 130, 22), title, t.text);
        drawTopProcessLine(g, rect.X + 18, rect.Y + 39, L"CPU", metrics_.topCpu, row, t.accent);
        drawTopProcessLine(g, rect.X + 18, rect.Y + 64, L"MEM", metrics_.topMemory, row, t.accent2);
        drawTopProcessLine(g, rect.X + 18, rect.Y + 89, L"GPU", metrics_.topGpu, row, t.accent3);
    }

    void drawTopProcessLine(Graphics& g, REAL x, REAL y, const std::wstring& label, const std::vector<ProcessRow>& rows,
                            Font& font, Color accent) {
        const auto t = theme();
        SolidBrush dot(accent);
        g.FillEllipse(&dot, x, y + 5.0f, 8.0f, 8.0f);
        drawText(g, label, RectF(x + 14, y, 36, 18), font, t.muted);
        if (rows.empty()) {
            drawText(g, L"N/A", RectF(x + 54, y, 260, 18), font, t.subtle);
            return;
        }

        std::wstring names;
        for (size_t i = 0; i < std::min<size_t>(2, rows.size()); ++i) {
            if (!names.empty()) {
                names += L"  ";
            }
            names += rows[i].name;
            names += L" ";
            if (label == L"CPU") {
                names += formatOneDecimalPercent(rows[i].cpu);
            } else if (label == L"MEM") {
                names += formatBytes(static_cast<double>(rows[i].memory));
            } else {
                names += formatOneDecimalPercent(rows[i].gpu);
            }
        }
        drawText(g, names, RectF(x + 54, y, 318, 18), font, t.text);
    }

    void drawSmallStatCard(Graphics& g, RectF rect, const std::wstring& title, const std::wstring& primary,
                           const std::wstring& secondary, Color accent, CardIcon icon) {
        const auto t = theme();
        drawPanel(g, rect);
        Font label = makeFont(15, FontStyleBold);

        drawCardIcon(g, RectF(rect.X + rect.Width - 42, rect.Y + 12, 24, 24), icon);
        drawText(g, title, RectF(rect.X + 18, rect.Y + 12, rect.Width - 62, 20), label, t.text);
        drawLegend(g, rect.X + 18, rect.Y + 34, primary, accent);
        drawLegend(g, rect.X + 18, rect.Y + rect.Height - 20, secondary, t.muted);
    }

    void drawInfoStrip(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font label = makeFont(14, FontStyleBold);
        Font value = makeFont(13, FontStyleBold);

        drawText(g, L"I/O", RectF(rect.X + 18, rect.Y + 14, 60, 20), label, t.text);
        drawText(g, L"Write " + (metrics_.diskWrite >= 0 ? formatSpeed(metrics_.diskWrite) : L"N/A"),
                 RectF(rect.X + 18, rect.Y + 40, 150, 22), value, t.muted);
        drawText(g, L"Disk " + formatPercent(metrics_.disk), RectF(rect.X + rect.Width - 118, rect.Y + 14, 96, 22), value,
                 t.muted, StringAlignmentFar);
        drawText(g, L"Net " + formatPercent(metrics_.network), RectF(rect.X + rect.Width - 118, rect.Y + 40, 96, 22), value,
                 t.muted, StringAlignmentFar);
    }

    void drawFooter(Graphics& g, int width, int height) {
        const auto t = theme();
        const REAL y = static_cast<REAL>(height - 48);
        for (int i = 0; i < 3; ++i) {
            RectF rect(width - 166.0f + i * 54.0f, y, 38, 38);
            auto path = roundedRect(rect, 8.0f);
            SolidBrush bg(t.buttonBg);
            g.FillPath(&bg, path.get());
            Pen border(t.panelEdge, 1.0f);
            g.DrawPath(&border, path.get());
            Pen pen(t.accent, 2.0f);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            const REAL cx = rect.X + rect.Width / 2.0f;
            const REAL cy = rect.Y + rect.Height / 2.0f;
            if (i == 0) {
                g.DrawLine(&pen, cx - 8, cy, cx - 3, cy);
                g.DrawLine(&pen, cx - 3, cy, cx, cy - 7);
                g.DrawLine(&pen, cx, cy - 7, cx + 4, cy + 7);
                g.DrawLine(&pen, cx + 4, cy + 7, cx + 9, cy);
            } else if (i == 1) {
                g.DrawEllipse(&pen, cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
                g.DrawEllipse(&pen, cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
                for (int tooth = 0; tooth < 6; ++tooth) {
                    const REAL angle = static_cast<REAL>(tooth) * 3.14159f / 3.0f;
                    const REAL x1 = cx + std::cos(angle) * 10.0f;
                    const REAL y1 = cy + std::sin(angle) * 10.0f;
                    const REAL x2 = cx + std::cos(angle) * 12.0f;
                    const REAL y2 = cy + std::sin(angle) * 12.0f;
                    g.DrawLine(&pen, x1, y1, x2, y2);
                }
            } else {
                g.DrawArc(&pen, cx - 9.0f, cy - 6.0f, 18.0f, 18.0f, 35.0f, 290.0f);
                g.DrawLine(&pen, cx, cy - 12.0f, cx, cy - 1.0f);
            }
        }
    }

    void drawLegend(Graphics& g, REAL x, REAL y, const std::wstring& text, Color accent) {
        const auto t = theme();
        SolidBrush dot(accent);
        g.FillEllipse(&dot, x, y + 4.0f, 8.0f, 8.0f);
        Font font = makeFont(12, FontStyleBold);
        drawText(g, text, RectF(x + 14, y, 150, 18), font, t.text);
    }

    void drawSparkline(Graphics& g, RectF rect, const SampleHistory& history, Color accent) {
        const auto t = theme();
        Pen grid(t.panelEdge, 1.0f);
        g.DrawLine(&grid, rect.X, rect.Y + rect.Height, rect.X + rect.Width, rect.Y + rect.Height);

        if (history.values.size() < 2) {
            return;
        }

        GraphicsPath fillPath;
        GraphicsPath linePath;
        const auto count = history.values.size();
        PointF previousLine{};
        PointF previousFill{};
        for (size_t i = 0; i < count; ++i) {
            const REAL x = rect.X + static_cast<REAL>(i) * rect.Width / static_cast<REAL>(kHistorySize - 1);
            const REAL y = rect.Y + rect.Height -
                           static_cast<REAL>(history.values[i]) * (rect.Height - 2.0f);
            PointF point(x, y);
            if (i == 0) {
                linePath.StartFigure();
                linePath.AddLine(x, y, x, y);
                fillPath.StartFigure();
                fillPath.AddLine(x, rect.Y + rect.Height, x, y);
                previousLine = point;
                previousFill = point;
            } else {
                linePath.AddLine(previousLine, point);
                fillPath.AddLine(previousFill, point);
                previousLine = point;
                previousFill = point;
            }
        }
        fillPath.AddLine(rect.X + rect.Width, rect.Y + rect.Height, rect.X, rect.Y + rect.Height);
        fillPath.CloseFigure();

        SolidBrush area(t.sparkArea);
        Pen pen(accent, 2.0f);
        g.FillPath(&area, &fillPath);
        g.DrawPath(&pen, &linePath);
    }

    void drawProcessPanel(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font title = makeFont(18, FontStyleBold);
        Font header = makeFont(12, FontStyleBold);
        Font row = makeFont(13, FontStyleRegular);

        drawText(g, L"内存占用最高的进程", RectF(rect.X + 20, rect.Y + 18, rect.Width - 40, 28), title,
                 t.text);
        drawText(g, L"进程", RectF(rect.X + 22, rect.Y + 62, 180, 20), header, t.muted);
        drawText(g, L"PID", RectF(rect.X + rect.Width - 172, rect.Y + 62, 50, 20), header, t.muted);
        drawText(g, L"内存", RectF(rect.X + rect.Width - 108, rect.Y + 62, 86, 20), header, t.muted,
                 StringAlignmentFar);

        REAL y = rect.Y + 90;
        const REAL rowHeight = 30;
        for (const auto& process : metrics_.topMemory) {
            SolidBrush rowBg(t.buttonBg);
            if (static_cast<int>((y - rect.Y) / rowHeight) % 2 == 0) {
                auto rowPath = roundedRect(RectF(rect.X + 14, y - 4, rect.Width - 28, rowHeight), 6.0f);
                g.FillPath(&rowBg, rowPath.get());
            }

            drawText(g, process.name, RectF(rect.X + 22, y, rect.Width - 230, 22), row, t.text);

            wchar_t pid[24];
            swprintf(pid, 24, L"%lu", process.pid);
            drawText(g, pid, RectF(rect.X + rect.Width - 172, y, 50, 22), row, t.subtle);
            drawText(g, formatBytes(static_cast<double>(process.memory)), RectF(rect.X + rect.Width - 128, y, 106, 22), row,
                     t.text, StringAlignmentFar);
            y += rowHeight;
        }
    }

    void drawInfoPanel(Graphics& g, RectF rect) {
        const auto t = theme();
        drawPanel(g, rect);
        Font title = makeFont(18, FontStyleBold);
        Font label = makeFont(12, FontStyleBold);
        Font value = makeFont(15, FontStyleRegular);

        drawText(g, L"系统细节", RectF(rect.X + 20, rect.Y + 18, rect.Width - 40, 28), title,
                 t.text);

        REAL y = rect.Y + 66;
        drawInfoRow(g, rect, y, L"GPU", metrics_.gpuName, label, value);
        y += 58;
        drawInfoRow(g, rect, y, L"磁盘读取", metrics_.diskRead >= 0 ? formatSpeed(metrics_.diskRead) : L"N/A", label, value);
        y += 58;
        drawInfoRow(g, rect, y, L"磁盘写入", metrics_.diskWrite >= 0 ? formatSpeed(metrics_.diskWrite) : L"N/A", label, value);
        y += 58;
        drawInfoRow(g, rect, y, L"网络下载", formatSpeed(metrics_.netDown), label, value);
        y += 58;
        drawInfoRow(g, rect, y, L"网络上传", formatSpeed(metrics_.netUp), label, value);
    }

    void drawInfoRow(Graphics& g, RectF panel, REAL y, const std::wstring& labelText, const std::wstring& valueText,
                     Font& labelFont, Font& valueFont) {
        const auto t = theme();
        RectF rowRect(panel.X + 16, y - 8, panel.Width - 32, 46);
        auto path = roundedRect(rowRect, 6.0f);
        SolidBrush bg(t.buttonBg);
        g.FillPath(&bg, path.get());
        drawText(g, labelText, RectF(panel.X + 28, y, panel.Width - 56, 16), labelFont, t.muted);
        drawText(g, valueText, RectF(panel.X + 28, y + 17, panel.Width - 56, 22), valueFont, t.text);
    }
};

} // namespace

int runMiniMonitor(HINSTANCE instance) {
    enableDpiAwareness();

    HANDLE singleton = CreateMutexW(nullptr, TRUE, kSingletonMutexName);
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kClassName, kAppTitle);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleton);
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        if (singleton) {
            ReleaseMutex(singleton);
            CloseHandle(singleton);
        }
        return 1;
    }

    AppWindow app(instance);
    if (!app.create()) {
        GdiplusShutdown(gdiplusToken);
        if (singleton) {
            ReleaseMutex(singleton);
            CloseHandle(singleton);
        }
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    GdiplusShutdown(gdiplusToken);
    if (singleton) {
        ReleaseMutex(singleton);
        CloseHandle(singleton);
    }
    return static_cast<int>(message.wParam);
}
