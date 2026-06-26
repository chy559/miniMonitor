#pragma once

#include <windows.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

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
    std::wstring lastUpdated = L"Not updated";
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

enum class CardIcon {
    Cpu,
    Gpu,
    Memory,
    Codex,
    Apps,
    Network,
    Disk,
};

enum class UiTheme : DWORD {
    Mono = 0,
    Ocean = 1,
    Sakura = 2,
    Forest = 3,
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
    std::wstring diskRoot = L"C:\\";
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
