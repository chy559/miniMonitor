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

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr UINT_PTR kRefreshTimer = 1001;
constexpr UINT kTrayMessage = WM_APP + 42;
constexpr wchar_t kClassName[] = L"MiniMonitorWindow";
constexpr wchar_t kAppTitle[] = L"MiniMonitor";
constexpr int kPanelWidth = 430;
constexpr int kPanelHeight = 780;
constexpr int kHistorySize = 64;

struct SampleHistory {
    std::deque<double> values;

    void push(double value) {
        value = std::clamp(value, 0.0, 1.0);
        if (values.size() >= kHistorySize) {
            values.pop_front();
        }
        values.push_back(value);
    }
};

struct ProcessRow {
    std::wstring name;
    DWORD pid = 0;
    SIZE_T memory = 0;
    double cpu = 0.0;
    double gpu = -1.0;
};

struct CodexQuota {
    bool checked = false;
    bool available = false;
    bool localAuth = false;
    std::wstring status = L"Not checked";
    std::wstring firstLabel = L"5h";
    std::wstring fiveHour = L"N/A";
    std::wstring fiveHourReset = L"N/A";
    std::wstring secondLabel = L"Weekly";
    std::wstring sevenDay = L"N/A";
    std::wstring sevenDayReset = L"N/A";
    std::wstring firstUsage = L"N/A";
    std::wstring secondUsage = L"N/A";
    double firstProgress = 0.0;
    double secondProgress = 0.0;
    ULONGLONG lastCheckedTick = 0;
};

struct QuotaWindow {
    bool available = false;
    double usedPercent = 0.0;
    double windowSeconds = 0.0;
    double resetAt = 0.0;
    std::wstring label = L"--";
    std::wstring remaining = L"N/A";
    std::wstring reset = L"N/A";
    std::wstring usage = L"N/A";
    double progress = 0.0;
};

struct Metrics {
    double cpu = 0.0;
    double gpu = -1.0;
    double memory = 0.0;
    double disk = 0.0;
    double network = 0.0;
    double diskRead = -1.0;
    double diskWrite = -1.0;
    double netDown = 0.0;
    double netUp = 0.0;
    unsigned long long memoryUsed = 0;
    unsigned long long memoryTotal = 0;
    unsigned long long diskUsed = 0;
    unsigned long long diskTotal = 0;
    std::wstring gpuName = L"N/A";
    std::vector<ProcessRow> topMemory;
    std::vector<ProcessRow> topCpu;
    std::vector<ProcessRow> topGpu;
    CodexQuota quota;
};

struct CpuSnapshot {
    ULONGLONG idle = 0;
    ULONGLONG kernel = 0;
    ULONGLONG user = 0;
    bool valid = false;
};

struct NetworkSnapshot {
    unsigned long long inBytes = 0;
    unsigned long long outBytes = 0;
    ULONGLONG tick = 0;
    bool valid = false;
};

ULONGLONG fileTimeToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER li{};
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

std::wstring formatBytes(double bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 4) {
        bytes /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64];
    if (unit == 0) {
        swprintf(buffer, 64, L"%.0f %s", bytes, units[unit]);
    } else {
        swprintf(buffer, 64, L"%.1f %s", bytes, units[unit]);
    }
    return buffer;
}

std::wstring formatSpeed(double bytesPerSecond) {
    return formatBytes(bytesPerSecond) + L"/s";
}

std::wstring formatPercent(double value) {
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
    return buffer;
}

std::wstring formatOneDecimalPercent(double value) {
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%.1f%%", std::max(0.0, value));
    return buffer;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return L"";
    }
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

bool readUtf8File(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
    CloseHandle(file);
    return ok;
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return "";
    }
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) {
        return "";
    }
    std::string out;
    bool escape = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char ch = json[i];
        if (escape) {
            out.push_back(ch);
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            break;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

bool extractNumberAfter(const std::string& json, size_t start, const std::vector<std::string>& keys, double& value) {
    size_t best = std::string::npos;
    for (const auto& key : keys) {
        size_t pos = json.find("\"" + key + "\"", start);
        if (pos != std::string::npos && (best == std::string::npos || pos < best)) {
            best = pos;
        }
    }
    if (best == std::string::npos || best > start + 900) {
        return false;
    }
    size_t colon = json.find(':', best);
    if (colon == std::string::npos) {
        return false;
    }
    size_t begin = json.find_first_of("-0123456789", colon + 1);
    if (begin == std::string::npos || begin > colon + 40) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(json.c_str() + begin, &end);
    return end && end != json.c_str() + begin;
}

size_t findJsonCloser(const std::string& json, size_t openPos, char openChar, char closeChar) {
    if (openPos >= json.size() || json[openPos] != openChar) {
        return std::string::npos;
    }

    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = openPos; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
        } else if (ch == openChar) {
            ++depth;
        } else if (ch == closeChar) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::string extractJsonObjectForKey(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return "";
    }
    size_t open = json.find('{', colon + 1);
    if (open == std::string::npos) {
        return "";
    }
    size_t close = findJsonCloser(json, open, '{', '}');
    if (close == std::string::npos) {
        return "";
    }
    return json.substr(open, close - open + 1);
}

std::string extractFirstArrayString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return "";
    }
    size_t colon = json.find(':', keyPos + needle.size());
    size_t open = colon == std::string::npos ? std::string::npos : json.find('[', colon + 1);
    if (open == std::string::npos) {
        return "";
    }
    size_t quote = json.find('"', open + 1);
    if (quote == std::string::npos) {
        return "";
    }

    std::string out;
    bool escape = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escape) {
            out.push_back(ch);
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else if (ch == '"') {
            break;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::wstring formatResetDetail(double value) {
    if (value <= 0.0) {
        return L"reset N/A";
    }
    if (value > 100000000000.0) {
        value /= 1000.0;
    }

    std::time_t resetTime = static_cast<std::time_t>(value);
    std::time_t nowTime = std::time(nullptr);
    const double diff = std::max(0.0, std::difftime(resetTime, nowTime));
    const int totalMinutes = static_cast<int>(std::round(diff / 60.0));
    const int days = totalMinutes / (24 * 60);
    const int hours = (totalMinutes % (24 * 60)) / 60;
    const int minutes = totalMinutes % 60;

    std::wstring remaining;
    if (days > 0) {
        remaining = std::to_wstring(days) + L"d " + std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
    } else if (hours > 0) {
        remaining = std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
    } else {
        remaining = std::to_wstring(minutes) + L"m";
    }

    std::tm resetLocal{};
    if (localtime_s(&resetLocal, &resetTime) != 0) {
        return remaining;
    }

    wchar_t dateBuffer[32]{};
    swprintf(dateBuffer, 32, L"%02d/%02d %02d:%02d", resetLocal.tm_mon + 1, resetLocal.tm_mday,
             resetLocal.tm_hour, resetLocal.tm_min);
    return remaining + L" (" + dateBuffer + L")";
}

std::wstring quotaWindowLabel(double seconds) {
    const int rounded = static_cast<int>(std::round(seconds));
    if (std::abs(rounded - 5 * 60 * 60) <= 60) {
        return L"5h";
    }
    if (std::abs(rounded - 7 * 24 * 60 * 60) <= 60) {
        return L"Weekly";
    }
    if (rounded >= 24 * 60 * 60 && rounded % (24 * 60 * 60) == 0) {
        return std::to_wstring(rounded / (24 * 60 * 60)) + L"d";
    }
    if (rounded >= 60 * 60 && rounded % (60 * 60) == 0) {
        return std::to_wstring(rounded / (60 * 60)) + L"h";
    }
    if (rounded >= 60 && rounded % 60 == 0) {
        return std::to_wstring(rounded / 60) + L"m";
    }
    return L"Limit";
}

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

Color colorFromHex(BYTE r, BYTE g, BYTE b, BYTE a = 255) {
    return Color(a, r, g, b);
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

class SystemSampler {
public:
    SystemSampler() {
        queryDiskCounters();
        queryGpuCounters();
        gpuName_ = detectGpuName();
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        processorCount_ = std::max<DWORD>(1, info.dwNumberOfProcessors);
    }

    ~SystemSampler() {
        if (pdhQuery_) {
            PdhCloseQuery(pdhQuery_);
        }
        if (gpuQuery_) {
            PdhCloseQuery(gpuQuery_);
        }
    }

    Metrics collect() {
        Metrics metrics;
        metrics.cpu = sampleCpu();
        sampleMemory(metrics);
        sampleDisk(metrics);
        sampleNetwork(metrics);
        sampleDiskIo(metrics);
        metrics.gpuName = gpuName_;
        sampleProcesses(metrics);
        sampleGpuProcesses(metrics);
        sampleCodexQuota(metrics);
        return metrics;
    }

private:
    CpuSnapshot cpu_;
    NetworkSnapshot network_;
    PDH_HQUERY pdhQuery_ = nullptr;
    PDH_HCOUNTER diskReadCounter_ = nullptr;
    PDH_HCOUNTER diskWriteCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    std::wstring gpuName_;
    DWORD processorCount_ = 1;
    ULONGLONG processSampleTick_ = 0;
    std::map<DWORD, ULONGLONG> previousProcessTimes_;
    CodexQuota cachedQuota_;

    double sampleCpu() {
        FILETIME idleTime{}, kernelTime{}, userTime{};
        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            return 0.0;
        }

        CpuSnapshot current{
            fileTimeToUInt64(idleTime),
            fileTimeToUInt64(kernelTime),
            fileTimeToUInt64(userTime),
            true,
        };

        double usage = 0.0;
        if (cpu_.valid) {
            const auto idleDelta = current.idle - cpu_.idle;
            const auto kernelDelta = current.kernel - cpu_.kernel;
            const auto userDelta = current.user - cpu_.user;
            const auto total = kernelDelta + userDelta;
            if (total > 0) {
                usage = 1.0 - (static_cast<double>(idleDelta) / static_cast<double>(total));
            }
        }
        cpu_ = current;
        return std::clamp(usage, 0.0, 1.0);
    }

    void sampleMemory(Metrics& metrics) {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            return;
        }
        metrics.memoryTotal = status.ullTotalPhys;
        metrics.memoryUsed = status.ullTotalPhys - status.ullAvailPhys;
        metrics.memory = static_cast<double>(metrics.memoryUsed) / static_cast<double>(status.ullTotalPhys);
    }

    void sampleDisk(Metrics& metrics) {
        ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFreeBytes{};
        if (!GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFreeBytes)) {
            return;
        }
        metrics.diskTotal = totalBytes.QuadPart;
        metrics.diskUsed = totalBytes.QuadPart - totalFreeBytes.QuadPart;
        metrics.disk = static_cast<double>(metrics.diskUsed) / static_cast<double>(metrics.diskTotal);
    }

    void sampleNetwork(Metrics& metrics) {
        unsigned long long inBytes = 0;
        unsigned long long outBytes = 0;
        DWORD tableSize = 0;
        if (GetIfTable(nullptr, &tableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER && tableSize > 0) {
            std::vector<BYTE> buffer(tableSize);
            auto table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());
            if (GetIfTable(table, &tableSize, FALSE) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL || row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
                        continue;
                    }
                    inBytes += row.dwInOctets;
                    outBytes += row.dwOutOctets;
                }
            }
        }

        NetworkSnapshot current{inBytes, outBytes, GetTickCount64(), true};
        if (network_.valid && current.tick > network_.tick) {
            const double seconds = static_cast<double>(current.tick - network_.tick) / 1000.0;
            if (current.inBytes >= network_.inBytes) {
                metrics.netDown = static_cast<double>(current.inBytes - network_.inBytes) / seconds;
            }
            if (current.outBytes >= network_.outBytes) {
                metrics.netUp = static_cast<double>(current.outBytes - network_.outBytes) / seconds;
            }
        }
        network_ = current;
        const double peak = 20.0 * 1024.0 * 1024.0;
        metrics.network = std::clamp((metrics.netDown + metrics.netUp) / peak, 0.0, 1.0);
    }

    void queryDiskCounters() {
        if (PdhOpenQueryW(nullptr, 0, &pdhQuery_) != ERROR_SUCCESS) {
            pdhQuery_ = nullptr;
            return;
        }

        using AddEnglishCounter = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        AddEnglishCounter addEnglishCounter = nullptr;
        if (pdh) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            addEnglishCounter = reinterpret_cast<AddEnglishCounter>(GetProcAddress(pdh, "PdhAddEnglishCounterW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        const auto addCounter = addEnglishCounter ? addEnglishCounter : PdhAddCounterW;

        const auto readStatus = addCounter(
            pdhQuery_, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &diskReadCounter_);
        const auto writeStatus = addCounter(
            pdhQuery_, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &diskWriteCounter_);

        if (readStatus != ERROR_SUCCESS || writeStatus != ERROR_SUCCESS) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
            diskReadCounter_ = nullptr;
            diskWriteCounter_ = nullptr;
            return;
        }
        PdhCollectQueryData(pdhQuery_);
    }

    void queryGpuCounters() {
        if (PdhOpenQueryW(nullptr, 0, &gpuQuery_) != ERROR_SUCCESS) {
            gpuQuery_ = nullptr;
            return;
        }

        using AddEnglishCounter = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        AddEnglishCounter addEnglishCounter = nullptr;
        if (pdh) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            addEnglishCounter = reinterpret_cast<AddEnglishCounter>(GetProcAddress(pdh, "PdhAddEnglishCounterW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        const auto addCounter = addEnglishCounter ? addEnglishCounter : PdhAddCounterW;

        const auto status = addCounter(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
        if (pdh) {
            FreeLibrary(pdh);
        }
        if (status != ERROR_SUCCESS) {
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            gpuCounter_ = nullptr;
            return;
        }
        PdhCollectQueryData(gpuQuery_);
    }

    void sampleDiskIo(Metrics& metrics) {
        if (!pdhQuery_) {
            return;
        }
        if (PdhCollectQueryData(pdhQuery_) != ERROR_SUCCESS) {
            return;
        }

        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(diskReadCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == ERROR_SUCCESS) {
            metrics.diskRead = std::max(0.0, value.doubleValue);
        }
        if (PdhGetFormattedCounterValue(diskWriteCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == ERROR_SUCCESS) {
            metrics.diskWrite = std::max(0.0, value.doubleValue);
        }
    }

    std::wstring detectGpuName() {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index) {
            if ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0) {
                return device.DeviceString;
            }
            device.cb = sizeof(device);
        }
        device.cb = sizeof(device);
        if (EnumDisplayDevicesW(nullptr, 0, &device, 0)) {
            return device.DeviceString;
        }
        return L"N/A";
    }

    void sampleProcesses(Metrics& metrics) {
        std::vector<ProcessRow> rows;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const double elapsed = processSampleTick_ > 0 && now > processSampleTick_
            ? static_cast<double>(now - processSampleTick_) / 1000.0
            : 0.0;
        std::map<DWORD, ULONGLONG> currentTimes;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                PROCESS_MEMORY_COUNTERS pmc{};
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
                if (process) {
                    FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
                    ULONGLONG procTime = 0;
                    if (GetProcessTimes(process, &createTime, &exitTime, &kernelTime, &userTime)) {
                        procTime = fileTimeToUInt64(kernelTime) + fileTimeToUInt64(userTime);
                        currentTimes[entry.th32ProcessID] = procTime;
                    }
                    if (GetProcessMemoryInfo(process, &pmc, sizeof(pmc))) {
                        double cpu = 0.0;
                        auto previous = previousProcessTimes_.find(entry.th32ProcessID);
                        if (elapsed > 0.0 && previous != previousProcessTimes_.end() && procTime >= previous->second) {
                            cpu = ((static_cast<double>(procTime - previous->second) / 10000000.0) / elapsed) *
                                  100.0 / static_cast<double>(processorCount_);
                        }
                        rows.push_back({entry.szExeFile, entry.th32ProcessID, pmc.WorkingSetSize, cpu, -1.0});
                    }
                    CloseHandle(process);
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        previousProcessTimes_ = std::move(currentTimes);
        processSampleTick_ = now;

        metrics.topMemory = rows;
        std::sort(metrics.topMemory.begin(), metrics.topMemory.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.memory > b.memory;
        });
        if (metrics.topMemory.size() > 3) {
            metrics.topMemory.resize(3);
        }

        metrics.topCpu = std::move(rows);
        std::sort(metrics.topCpu.begin(), metrics.topCpu.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.cpu > b.cpu;
        });
        if (metrics.topCpu.size() > 3) {
            metrics.topCpu.resize(3);
        }
    }

    void sampleGpuProcesses(Metrics& metrics) {
        if (!gpuQuery_ || !gpuCounter_) {
            return;
        }
        if (PdhCollectQueryData(gpuQuery_) != ERROR_SUCCESS) {
            return;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bufferSize == 0 || itemCount == 0) {
            return;
        }

        std::vector<BYTE> buffer(bufferSize);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            return;
        }

        std::map<DWORD, double> usageByPid;
        double totalGpu = 0.0;
        for (DWORD i = 0; i < itemCount; ++i) {
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !items[i].szName) {
                continue;
            }
            const double usage = std::max(0.0, items[i].FmtValue.doubleValue);
            totalGpu += usage;

            std::wstring instance = items[i].szName;
            size_t pidPos = instance.find(L"pid_");
            if (pidPos == std::wstring::npos) {
                continue;
            }
            pidPos += 4;
            DWORD pid = 0;
            while (pidPos < instance.size() && iswdigit(instance[pidPos])) {
                pid = pid * 10 + static_cast<DWORD>(instance[pidPos] - L'0');
                ++pidPos;
            }
            if (pid != 0) {
                usageByPid[pid] += usage;
            }
        }
        metrics.gpu = std::clamp(totalGpu / 100.0, 0.0, 1.0);

        for (const auto& [pid, gpu] : usageByPid) {
            if (gpu <= 0.05) {
                continue;
            }
            ProcessRow row;
            row.pid = pid;
            row.gpu = gpu;
            row.name = processName(pid);
            metrics.topGpu.push_back(row);
        }
        std::sort(metrics.topGpu.begin(), metrics.topGpu.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.gpu > b.gpu;
        });
        if (metrics.topGpu.size() > 3) {
            metrics.topGpu.resize(3);
        }
    }

    std::wstring processName(DWORD pid) {
        std::wstring name = L"pid " + std::to_wstring(pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            return name;
        }
        wchar_t buffer[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
            std::wstring path(buffer, size);
            size_t slash = path.find_last_of(L"\\/");
            name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
        CloseHandle(process);
        return name;
    }

    void sampleCodexQuota(Metrics& metrics) {
        const ULONGLONG now = GetTickCount64();
        if (cachedQuota_.checked && now >= cachedQuota_.lastCheckedTick &&
            now - cachedQuota_.lastCheckedTick < 5ULL * 60ULL * 1000ULL) {
            metrics.quota = cachedQuota_;
            return;
        }

        cachedQuota_ = fetchCodexQuota();
        cachedQuota_.lastCheckedTick = now;
        metrics.quota = cachedQuota_;
    }

    CodexQuota fetchCodexQuota() {
        CodexQuota quota;
        quota.checked = true;
        quota.status = L"Auth not found";

        wchar_t userProfile[MAX_PATH]{};
        DWORD userProfileSize = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
        if (userProfileSize == 0 || userProfileSize >= MAX_PATH) {
            return quota;
        }

        std::wstring authPath = std::wstring(userProfile) + L"\\.codex\\auth.json";
        std::string authJson;
        if (!readUtf8File(authPath, authJson)) {
            return quota;
        }

        std::string accessToken = extractJsonString(authJson, "access_token");
        std::string accountId = extractJsonString(authJson, "account_id");
        if (accessToken.empty()) {
            quota.status = L"Auth incomplete";
            return quota;
        }

        const std::vector<std::wstring> paths = {
            L"/backend-api/wham/usage",
            L"/backend-api/wham/accounts/check"
        };
        for (const auto& path : paths) {
            DWORD statusCode = 0;
            std::string body = httpGetChatGpt(path, accessToken, accountId, statusCode);
            if (statusCode >= 200 && statusCode < 300 && !body.empty()) {
                parseQuotaBody(body, quota);
                if (quota.available) {
                    return quota;
                }
                quota.status = L"Quota unreadable";
            } else if (statusCode == 401 || statusCode == 403) {
                quota.status = L"Login expired";
                return quota;
            } else if (statusCode > 0) {
                quota.status = L"Quota unavailable";
            }
        }
        if (!quota.available && quota.status == L"Auth not found") {
            quota.status = L"Quota unavailable";
        }
        return quota;
    }

    std::string httpGetChatGpt(const std::wstring& path, const std::string& accessToken,
                               const std::string& accountId, DWORD& statusCode) {
        statusCode = 0;
        std::string response;

        HINTERNET session = WinHttpOpen(L"MiniMonitor/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            return response;
        }
        WinHttpSetTimeouts(session, 2000, 2000, 2500, 2500);

        HINTERNET connect = WinHttpConnect(session, L"chatgpt.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            return response;
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return response;
        }

        std::string authHeader = "Authorization: Bearer " + accessToken +
                                 "\r\nAccept: application/json"
                                 "\r\nOAI-Product-Sku: CODEX"
                                 "\r\nOAI-Language: en"
                                 "\r\noriginator: Codex Desktop";
        if (!accountId.empty()) {
            authHeader += "\r\nChatGPT-Account-Id: " + accountId;
        }
        authHeader += "\r\n";
        std::wstring headers = utf8ToWide(authHeader);
        BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                  WinHttpReceiveResponse(request, nullptr);

        if (ok) {
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0 && response.size() < 1024 * 1024) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    break;
                }
                chunk.resize(read);
                response += chunk;
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return response;
    }

    void parseQuotaBody(const std::string& body, CodexQuota& quota) {
        std::string rateLimit = extractJsonObjectForKey(body, "rate_limit");
        if (rateLimit.empty()) {
            std::string account = selectedCodexAccount(body);
            if (!account.empty()) {
                rateLimit = extractJsonObjectForKey(account, "rate_limit");
            }
        }
        if (rateLimit.empty()) {
            quota.status = L"No quota";
            return;
        }

        QuotaWindow primary = parseLimitWindow(rateLimit, "primary_window");
        QuotaWindow secondary = parseLimitWindow(rateLimit, "secondary_window");

        if (!primary.available && !secondary.available) {
            quota.status = L"Quota unreadable";
            return;
        }

        quota.available = true;
        quota.localAuth = false;
        quota.status = L"Codex quota";
        if (primary.available) {
            quota.firstLabel = primary.label;
            quota.fiveHour = primary.remaining;
            quota.fiveHourReset = primary.reset;
            quota.firstUsage = primary.usage;
            quota.firstProgress = primary.progress;
        }
        if (secondary.available) {
            quota.secondLabel = secondary.label;
            quota.sevenDay = secondary.remaining;
            quota.sevenDayReset = secondary.reset;
            quota.secondUsage = secondary.usage;
            quota.secondProgress = secondary.progress;
        }
    }

    std::string selectedCodexAccount(const std::string& body) {
        std::string selectedId = extractFirstArrayString(body, "account_ordering");
        size_t accountsKey = body.find("\"accounts\"");
        if (accountsKey == std::string::npos) {
            return "";
        }
        size_t colon = body.find(':', accountsKey);
        size_t arrayOpen = colon == std::string::npos ? std::string::npos : body.find('[', colon + 1);
        if (arrayOpen == std::string::npos) {
            return "";
        }
        size_t arrayClose = findJsonCloser(body, arrayOpen, '[', ']');
        if (arrayClose == std::string::npos) {
            return "";
        }

        std::string fallback;
        for (size_t pos = arrayOpen + 1; pos < arrayClose;) {
            size_t objectOpen = body.find('{', pos);
            if (objectOpen == std::string::npos || objectOpen >= arrayClose) {
                break;
            }
            size_t objectClose = findJsonCloser(body, objectOpen, '{', '}');
            if (objectClose == std::string::npos || objectClose > arrayClose) {
                break;
            }

            std::string object = body.substr(objectOpen, objectClose - objectOpen + 1);
            if (fallback.empty() && object.find("\"rate_limit\"") != std::string::npos) {
                fallback = object;
            }
            std::string id = extractJsonString(object, "id");
            if (!selectedId.empty() && id == selectedId) {
                return object;
            }
            pos = objectClose + 1;
        }
        return fallback;
    }

    QuotaWindow parseLimitWindow(const std::string& rateLimit, const std::string& key) {
        QuotaWindow window;
        std::string object = extractJsonObjectForKey(rateLimit, key);
        if (object.empty()) {
            return window;
        }

        double used = 0.0;
        if (!extractNumberAfter(object, 0, {"used_percent"}, used)) {
            return window;
        }

        double windowSeconds = 0.0;
        extractNumberAfter(object, 0, {"limit_window_seconds"}, windowSeconds);
        double resetAt = 0.0;
        extractNumberAfter(object, 0, {"reset_at"}, resetAt);

        const double usedFraction = used > 1.0 ? used / 100.0 : used;
        window.available = true;
        window.usedPercent = used;
        window.windowSeconds = windowSeconds;
        window.resetAt = resetAt;
        window.label = quotaWindowLabel(windowSeconds);
        window.remaining = formatPercent(1.0 - std::clamp(usedFraction, 0.0, 1.0)) + L" left";
        window.reset = formatResetDetail(resetAt);
        window.usage = formatPercent(std::clamp(usedFraction, 0.0, 1.0));
        window.progress = std::clamp(usedFraction, 0.0, 1.0);
        return window;
    }
};

class AppWindow {
public:
    AppWindow(HINSTANCE instance) : instance_(instance), icon_(createAppIcon()) {}

    ~AppWindow() {
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
        const int availableHeight = static_cast<int>(workArea.bottom - workArea.top - 32);
        const int panelHeight = std::min(kPanelHeight, std::max(560, availableHeight));
        const int panelX = workArea.right - kPanelWidth - 16;
        const int panelY = workArea.top + 16;

        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kClassName,
            kAppTitle,
            WS_POPUP,
            panelX,
            panelY,
            kPanelWidth,
            panelHeight,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return false;
        }

        SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, kPanelWidth, panelHeight, 24, 24), TRUE);
        SetTimer(hwnd_, kRefreshTimer, 2000, nullptr);
        addTrayIcon();
        metrics_ = sampler_.collect();
        updateHistory();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    SystemSampler sampler_;
    Metrics metrics_;
    std::wstring machineName_ = computerName();
    SampleHistory cpuHistory_;
    SampleHistory gpuHistory_;
    SampleHistory memoryHistory_;
    SampleHistory diskHistory_;
    SampleHistory networkHistory_;

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
                metrics_ = sampler_.collect();
                updateHistory();
                InvalidateRect(hwnd_, nullptr, FALSE);
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
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                ShowWindow(hwnd_, SW_HIDE);
            } else {
                RECT client{};
                GetClientRect(hwnd_, &client);
                SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, client.right, client.bottom, 24, 24), TRUE);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                ShowWindow(hwnd_, SW_SHOW);
                ShowWindow(hwnd_, SW_RESTORE);
                SetForegroundWindow(hwnd_);
            } else if (LOWORD(wParam) == 2) {
                DestroyWindow(hwnd_);
            }
            return 0;
        case kTrayMessage:
            handleTrayMessage(lParam);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kRefreshTimer);
            removeTrayIcon();
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

    void addTrayIcon() {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = icon_;
        wcscpy_s(nid.szTip, kAppTitle);
        Shell_NotifyIconW(NIM_ADD, &nid);
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
            ShowWindow(hwnd_, SW_SHOW);
            ShowWindow(hwnd_, SW_RESTORE);
            SetForegroundWindow(hwnd_);
        } else if (lParam == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"显示 MiniMonitor");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, L"退出");
            SetForegroundWindow(hwnd_);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
            DestroyMenu(menu);
        }
    }

    LRESULT hitTest(LPARAM lParam) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        RECT client{};
        GetClientRect(hwnd_, &client);
        if (point.y >= 0 && point.y < 92 && point.x >= 0 && point.x < client.right) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    void handleClick(int x, int y) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int bottom = client.bottom - 42;
        if (y < bottom) {
            return;
        }
        if (x >= client.right - 62) {
            DestroyWindow(hwnd_);
        } else if (x >= client.right - 116) {
            MessageBoxW(hwnd_, L"MiniMonitor\n2 秒刷新，托盘常驻。", L"MiniMonitor", MB_OK | MB_ICONINFORMATION);
        } else if (x >= client.right - 170) {
            ShowWindow(hwnd_, SW_HIDE);
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

        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        auto oldBitmap = SelectObject(memoryDc, bitmap);

        Graphics g(memoryDc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        drawBackground(g, width, height);
        drawHeader(g, width);
        drawCards(g, width, height);

        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        EndPaint(hwnd_, &ps);
    }

    void drawBackground(Graphics& g, int width, int height) {
        LinearGradientBrush bg(
            Rect(0, 0, width, height),
            colorFromHex(236, 231, 240),
            colorFromHex(224, 216, 229),
            LinearGradientModeForwardDiagonal);
        g.FillRectangle(&bg, 0, 0, width, height);

        SolidBrush washA(Color(22, 255, 255, 255));
        SolidBrush washB(Color(18, 118, 92, 165));
        SolidBrush washC(Color(16, 245, 184, 196));
        g.FillEllipse(&washA, -60, -80, 250, 220);
        g.FillEllipse(&washB, width - 150, 42, 210, 240);
        g.FillEllipse(&washC, 96, height - 160, 260, 220);
    }

    void drawHeader(Graphics& g, int) {
        Font title = makeFont(22, FontStyleBold);
        Font subtitle = makeFont(13, FontStyleBold);

        RectF iconRect(28, 24, 48, 48);
        auto iconPath = roundedRect(iconRect, 12.0f);
        LinearGradientBrush iconBrush(iconRect, colorFromHex(114, 148, 239), colorFromHex(169, 113, 220),
                                      LinearGradientModeForwardDiagonal);
        g.FillPath(&iconBrush, iconPath.get());
        Pen iconPen(Color(205, 255, 255, 255), 2.0f);
        g.DrawEllipse(&iconPen, iconRect.X + 12.0f, iconRect.Y + 12.0f, 24.0f, 24.0f);
        g.DrawLine(&iconPen, iconRect.X + 24, iconRect.Y + 18, iconRect.X + 24, iconRect.Y + 31);
        g.DrawLine(&iconPen, iconRect.X + 24, iconRect.Y + 31, iconRect.X + 32, iconRect.Y + 25);

        drawText(g, L"MiniMonitor", RectF(90, 24, 240, 28), title, colorFromHex(36, 34, 45));
        drawText(g, machineName_, RectF(91, 53, 260, 22), subtitle, colorFromHex(58, 53, 66));
    }

    void drawCards(Graphics& g, int width, int height) {
        const REAL margin = 18.0f;
        const REAL gap = 12.0f;
        const REAL top = 96.0f;
        const REAL halfW = (width - margin * 2.0f - gap) / 2.0f;

        drawMetricCard(g, RectF(margin, top, halfW, 132), L"CPU", formatPercent(metrics_.cpu),
                       L"系统负载", cpuHistory_, colorFromHex(83, 118, 224));
        drawGpuCard(g, RectF(margin + halfW + gap, top, halfW, 132));
        drawMemoryCard(g, RectF(margin, top + 144, width - margin * 2.0f, 104));

        const REAL quotaY = top + 260;
        drawQuotaCard(g, RectF(margin, quotaY, width - margin * 2.0f, 132));

        const REAL appsY = quotaY + 144;
        drawTopAppsCard(g, RectF(margin, appsY, width - margin * 2.0f, 116));

        const REAL rowY = appsY + 128;
        drawSmallStatCard(g, RectF(margin, rowY, halfW, 76), L"Network",
                          L"↓ " + formatSpeed(metrics_.netDown),
                          L"↑ " + formatSpeed(metrics_.netUp),
                          colorFromHex(74, 147, 224));
        drawSmallStatCard(g, RectF(margin + halfW + gap, rowY, halfW, 76), L"Disk",
                          formatBytes(static_cast<double>(metrics_.diskUsed)) + L" / " +
                              formatBytes(static_cast<double>(metrics_.diskTotal)),
                          L"Read " + (metrics_.diskRead >= 0 ? formatSpeed(metrics_.diskRead) : L"N/A"),
                          colorFromHex(248, 188, 77));
        drawFooter(g, width, height);
    }

    void drawPanel(Graphics& g, RectF rect) {
        auto path = roundedRect(rect, 8.0f);
        SolidBrush fill(Color(242, 250, 246, 252));
        Pen border(Color(170, 186, 174, 194), 1.0f);
        g.FillPath(&fill, path.get());
        g.DrawPath(&border, path.get());
    }

    void drawMetricCard(Graphics& g, RectF rect, const std::wstring& title, const std::wstring& value,
                        const std::wstring& subtitle, const SampleHistory& history, Color accent) {
        drawPanel(g, rect);

        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(28, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawText(g, title, RectF(rect.X + 18, rect.Y + 18, rect.Width - 116, 24), label, colorFromHex(35, 32, 43));
        drawText(g, value, RectF(rect.X + rect.Width - 116, rect.Y + 15, 98, 38), valueFont, colorFromHex(31, 29, 39),
                 StringAlignmentFar);
        drawText(g, subtitle, RectF(rect.X + 18, rect.Y + 47, rect.Width - 36, 20), small, colorFromHex(65, 58, 74));
        drawSparkline(g, RectF(rect.X + 18, rect.Y + 78, rect.Width - 36, 46), history, accent);
        drawLegend(g, rect.X + 18, rect.Y + rect.Height - 24, L"2s refresh", accent);
    }

    void drawGpuCard(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(26, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawText(g, L"GPU", RectF(rect.X + 18, rect.Y + 18, rect.Width - 36, 24), label, colorFromHex(35, 32, 43));
        drawText(g, metrics_.gpu >= 0.0 ? formatPercent(metrics_.gpu) : L"N/A",
                 RectF(rect.X + rect.Width - 92, rect.Y + 16, 74, 34), valueFont, colorFromHex(31, 29, 39),
                 StringAlignmentFar);
        drawText(g, metrics_.gpuName, RectF(rect.X + 18, rect.Y + 52, rect.Width - 36, 36), small, colorFromHex(65, 58, 74));

        drawSparkline(g, RectF(rect.X + 18, rect.Y + 82, rect.Width - 36, 32), gpuHistory_, colorFromHex(83, 118, 224));
        drawLegend(g, rect.X + 18, rect.Y + rect.Height - 24, L"GPU engine", colorFromHex(83, 118, 224));
    }

    void drawMemoryCard(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font label = makeFont(18, FontStyleBold);
        Font valueFont = makeFont(27, FontStyleBold);
        Font small = makeFont(12, FontStyleBold);

        drawText(g, L"Memory", RectF(rect.X + 20, rect.Y + 16, 170, 26), label, colorFromHex(35, 32, 43));
        drawText(g, formatPercent(metrics_.memory), RectF(rect.X + rect.Width - 118, rect.Y + 12, 96, 36), valueFont,
                 colorFromHex(31, 29, 39), StringAlignmentFar);

        RectF bar(rect.X + 20, rect.Y + 54, rect.Width - 40, 18);
        auto bgPath = roundedRect(bar, 6.0f);
        SolidBrush bg(Color(180, 226, 218, 230));
        g.FillPath(&bg, bgPath.get());
        RectF used = bar;
        used.Width *= static_cast<REAL>(metrics_.memory);
        auto usedPath = roundedRect(used, 6.0f);
        SolidBrush usedBrush(colorFromHex(99, 118, 225));
        g.FillPath(&usedBrush, usedPath.get());

        drawText(g,
                 formatBytes(static_cast<double>(metrics_.memoryUsed)) + L" / " +
                     formatBytes(static_cast<double>(metrics_.memoryTotal)),
                 RectF(rect.X + 20, rect.Y + 78, 170, 20), small, colorFromHex(55, 50, 64));
        drawSparkline(g, RectF(rect.X + rect.Width - 130, rect.Y + 76, 108, 18), memoryHistory_, colorFromHex(223, 157, 67));
    }

    void drawQuotaCard(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        std::wstring firstDetail = metrics_.quota.available ? metrics_.quota.fiveHourReset : metrics_.quota.status;
        std::wstring secondDetail = metrics_.quota.available ? metrics_.quota.sevenDayReset : L"";
        drawQuotaLine(g, RectF(rect.X + 18, rect.Y + 13, rect.Width - 36, 48), false, metrics_.quota.firstLabel,
                      metrics_.quota.firstUsage, metrics_.quota.firstProgress, firstDetail);
        drawQuotaLine(g, RectF(rect.X + 18, rect.Y + 72, rect.Width - 36, 48), true, metrics_.quota.secondLabel,
                      metrics_.quota.secondUsage, metrics_.quota.secondProgress, secondDetail);
    }

    void drawQuotaLine(Graphics& g, RectF rect, bool weekly, const std::wstring& window,
                       const std::wstring& usage, double progress, const std::wstring& reset) {
        Font label = makeFont(15, FontStyleRegular);
        Font percent = makeFont(15, FontStyleBold);
        Font detail = makeFont(12, FontStyleRegular);
        Color text = colorFromHex(67, 82, 104);
        Color orange = colorFromHex(245, 128, 10);

        drawQuotaIcon(g, rect.X, rect.Y + 1.0f, weekly, text);
        drawText(g, window, RectF(rect.X + 27, rect.Y - 1, 150, 22), label, text);
        drawText(g, usage, RectF(rect.X + rect.Width - 78, rect.Y - 1, 78, 22), percent, orange, StringAlignmentFar);

        RectF bar(rect.X, rect.Y + 27, rect.Width, 7);
        auto bgPath = roundedRect(bar, 3.5f);
        SolidBrush bg(Color(176, 226, 232, 238));
        g.FillPath(&bg, bgPath.get());
        if (progress > 0.0) {
            RectF fill = bar;
            fill.Width *= static_cast<REAL>(std::clamp(progress, 0.0, 1.0));
            auto fillPath = roundedRect(fill, 3.5f);
            SolidBrush fillBrush(orange);
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

    void drawTopAppsCard(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font title = makeFont(16, FontStyleBold);
        Font row = makeFont(12, FontStyleBold);

        drawText(g, L"Top Apps", RectF(rect.X + 18, rect.Y + 12, 130, 22), title, colorFromHex(35, 32, 43));
        drawTopProcessLine(g, rect.X + 18, rect.Y + 39, L"CPU", metrics_.topCpu, row, colorFromHex(83, 118, 224));
        drawTopProcessLine(g, rect.X + 18, rect.Y + 64, L"MEM", metrics_.topMemory, row, colorFromHex(99, 118, 225));
        drawTopProcessLine(g, rect.X + 18, rect.Y + 89, L"GPU", metrics_.topGpu, row, colorFromHex(95, 159, 118));
    }

    void drawTopProcessLine(Graphics& g, REAL x, REAL y, const std::wstring& label, const std::vector<ProcessRow>& rows,
                            Font& font, Color accent) {
        SolidBrush dot(accent);
        g.FillEllipse(&dot, x, y + 5.0f, 8.0f, 8.0f);
        drawText(g, label, RectF(x + 14, y, 36, 18), font, colorFromHex(65, 58, 74));
        if (rows.empty()) {
            drawText(g, L"N/A", RectF(x + 54, y, 260, 18), font, colorFromHex(112, 104, 118));
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
        drawText(g, names, RectF(x + 54, y, 318, 18), font, colorFromHex(48, 44, 58));
    }

    void drawSmallStatCard(Graphics& g, RectF rect, const std::wstring& title, const std::wstring& primary,
                           const std::wstring& secondary, Color accent) {
        drawPanel(g, rect);
        Font label = makeFont(15, FontStyleBold);
        Font value = makeFont(14, FontStyleBold);

        drawText(g, title, RectF(rect.X + 18, rect.Y + 12, rect.Width - 36, 20), label, colorFromHex(35, 32, 43));
        drawLegend(g, rect.X + 18, rect.Y + 34, primary, accent);
        drawLegend(g, rect.X + 18, rect.Y + 56, secondary, colorFromHex(87, 169, 130));

        RectF mini(rect.X + rect.Width - 56, rect.Y + 14, 32, 32);
        auto path = roundedRect(mini, 8.0f);
        SolidBrush brush(Color(82, accent.GetR(), accent.GetG(), accent.GetB()));
        g.FillPath(&brush, path.get());
        drawText(g, L"·", RectF(mini.X, mini.Y - 7, mini.Width, mini.Height), value, accent, StringAlignmentCenter,
                 StringAlignmentCenter);
    }

    void drawInfoStrip(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font label = makeFont(14, FontStyleBold);
        Font value = makeFont(13, FontStyleBold);

        drawText(g, L"I/O", RectF(rect.X + 18, rect.Y + 14, 60, 20), label, colorFromHex(35, 32, 43));
        drawText(g, L"Write " + (metrics_.diskWrite >= 0 ? formatSpeed(metrics_.diskWrite) : L"N/A"),
                 RectF(rect.X + 18, rect.Y + 40, 150, 22), value, colorFromHex(55, 50, 64));
        drawText(g, L"Disk " + formatPercent(metrics_.disk), RectF(rect.X + rect.Width - 118, rect.Y + 14, 96, 22), value,
                 colorFromHex(55, 50, 64), StringAlignmentFar);
        drawText(g, L"Net " + formatPercent(metrics_.network), RectF(rect.X + rect.Width - 118, rect.Y + 40, 96, 22), value,
                 colorFromHex(55, 50, 64), StringAlignmentFar);
    }

    void drawFooter(Graphics& g, int width, int height) {
        const REAL y = static_cast<REAL>(height - 48);
        for (int i = 0; i < 3; ++i) {
            RectF rect(width - 166.0f + i * 54.0f, y, 38, 38);
            auto path = roundedRect(rect, 8.0f);
            SolidBrush bg(Color(182, 255, 250, 255));
            g.FillPath(&bg, path.get());
            Pen pen(colorFromHex(66, 56, 72), 2.0f);
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
                for (int t = 0; t < 6; ++t) {
                    const REAL angle = static_cast<REAL>(t) * 3.14159f / 3.0f;
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
        SolidBrush dot(accent);
        g.FillEllipse(&dot, x, y + 4.0f, 8.0f, 8.0f);
        Font font = makeFont(12, FontStyleBold);
        drawText(g, text, RectF(x + 14, y, 150, 18), font, colorFromHex(48, 44, 58));
    }

    void drawSparkline(Graphics& g, RectF rect, const SampleHistory& history, Color accent) {
        Pen grid(Color(70, 186, 174, 194), 1.0f);
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

        SolidBrush area(Color(28, accent.GetR(), accent.GetG(), accent.GetB()));
        Pen pen(accent, 2.0f);
        g.FillPath(&area, &fillPath);
        g.DrawPath(&pen, &linePath);
    }

    void drawProcessPanel(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font title = makeFont(18, FontStyleBold);
        Font header = makeFont(12, FontStyleBold);
        Font row = makeFont(13, FontStyleRegular);

        drawText(g, L"内存占用最高的进程", RectF(rect.X + 20, rect.Y + 18, rect.Width - 40, 28), title,
                 colorFromHex(241, 245, 249));
        drawText(g, L"进程", RectF(rect.X + 22, rect.Y + 62, 180, 20), header, colorFromHex(126, 140, 164));
        drawText(g, L"PID", RectF(rect.X + rect.Width - 172, rect.Y + 62, 50, 20), header, colorFromHex(126, 140, 164));
        drawText(g, L"内存", RectF(rect.X + rect.Width - 108, rect.Y + 62, 86, 20), header, colorFromHex(126, 140, 164),
                 StringAlignmentFar);

        REAL y = rect.Y + 90;
        const REAL rowHeight = 30;
        for (const auto& process : metrics_.topMemory) {
            SolidBrush rowBg(Color(28, 255, 255, 255));
            if (static_cast<int>((y - rect.Y) / rowHeight) % 2 == 0) {
                auto rowPath = roundedRect(RectF(rect.X + 14, y - 4, rect.Width - 28, rowHeight), 6.0f);
                g.FillPath(&rowBg, rowPath.get());
            }

            drawText(g, process.name, RectF(rect.X + 22, y, rect.Width - 230, 22), row, colorFromHex(221, 227, 237));

            wchar_t pid[24];
            swprintf(pid, 24, L"%lu", process.pid);
            drawText(g, pid, RectF(rect.X + rect.Width - 172, y, 50, 22), row, colorFromHex(166, 179, 199));
            drawText(g, formatBytes(static_cast<double>(process.memory)), RectF(rect.X + rect.Width - 128, y, 106, 22), row,
                     colorFromHex(221, 227, 237), StringAlignmentFar);
            y += rowHeight;
        }
    }

    void drawInfoPanel(Graphics& g, RectF rect) {
        drawPanel(g, rect);
        Font title = makeFont(18, FontStyleBold);
        Font label = makeFont(12, FontStyleBold);
        Font value = makeFont(15, FontStyleRegular);

        drawText(g, L"系统细节", RectF(rect.X + 20, rect.Y + 18, rect.Width - 40, 28), title,
                 colorFromHex(241, 245, 249));

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
        RectF rowRect(panel.X + 16, y - 8, panel.Width - 32, 46);
        auto path = roundedRect(rowRect, 6.0f);
        SolidBrush bg(Color(34, 255, 255, 255));
        g.FillPath(&bg, path.get());
        drawText(g, labelText, RectF(panel.X + 28, y, panel.Width - 56, 16), labelFont, colorFromHex(126, 140, 164));
        drawText(g, valueText, RectF(panel.X + 28, y + 17, panel.Width - 56, 22), valueFont, colorFromHex(230, 236, 245));
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enableDpiAwareness();

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        return 1;
    }

    AppWindow app(instance);
    if (!app.create()) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(message.wParam);
}
