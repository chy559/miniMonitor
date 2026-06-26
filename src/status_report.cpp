#include "status_report.h"

#include "format_utils.h"

namespace {

std::wstring quotaSummaryText(const Metrics& metrics) {
    if (!metrics.quota.available) {
        return L"Codex: " + metrics.quota.status;
    }
    return L"Codex: 5h " + metrics.quota.firstUsage + L"    Weekly " + metrics.quota.secondUsage;
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

} // namespace

std::wstring buildStatusSummaryText(const Metrics& metrics, const std::wstring& diskDisplayName) {
    std::wstring text = L"MiniMonitor 当前状态\r\n";
    text += L"CPU: " + formatPercent(metrics.cpu);
    if (!metrics.topCpu.empty()) {
        text += L"  Top: " + metrics.topCpu.front().name + L" " + formatOneDecimalPercent(metrics.topCpu.front().cpu);
    }
    text += L"\r\nGPU: " + (metrics.gpu >= 0.0 ? formatPercent(metrics.gpu) : L"N/A");
    if (!metrics.topGpu.empty()) {
        text += L"  Top: " + metrics.topGpu.front().name + L" " + formatOneDecimalPercent(metrics.topGpu.front().gpu);
    }
    text += L"\r\nMemory: " + formatPercent(metrics.memory) + L"  " +
            formatBytes(static_cast<double>(metrics.memoryUsed)) + L" / " +
            formatBytes(static_cast<double>(metrics.memoryTotal));
    if (!metrics.topMemory.empty()) {
        text += L"  Top: " + metrics.topMemory.front().name + L" " +
                formatBytes(static_cast<double>(metrics.topMemory.front().memory));
    }
    text += L"\r\nNetwork: Down " + formatSpeed(metrics.netDown) + L"  Up " + formatSpeed(metrics.netUp);
    text += L"\r\nDisk " + diskDisplayName + L": Read " + (metrics.diskRead >= 0 ? formatSpeed(metrics.diskRead) : L"N/A") +
            L"  Write " + (metrics.diskWrite >= 0 ? formatSpeed(metrics.diskWrite) : L"N/A");
    text += L"\r\n" + quotaSummaryText(metrics);
    return text;
}

std::wstring buildStatusReportText(const Metrics& metrics, const StatusReportSettings& settings) {
    std::wstring text = buildStatusSummaryText(metrics, settings.diskDisplayName);
    text += L"\r\n\r\nTop Processes\r\n";
    text += processRowsReport(L"CPU", metrics.topCpu, L"cpu");
    text += processRowsReport(L"Memory", metrics.topMemory, L"memory");
    text += processRowsReport(L"GPU", metrics.topGpu, L"gpu");

    text += L"\r\nCodex\r\n";
    text += L"  Status: " + metrics.quota.status + L"\r\n";
    text += L"  " + metrics.quota.firstLabel + L": " + metrics.quota.firstUsage + L" / " +
            metrics.quota.fiveHourReset + L"\r\n";
    text += L"  " + metrics.quota.secondLabel + L": " + metrics.quota.secondUsage + L" / " +
            metrics.quota.sevenDayReset + L"\r\n";
    text += L"  Last updated: " + metrics.quota.lastUpdated + L"\r\n";

    text += L"\r\nSettings\r\n";
    text += L"  Theme: " + settings.themeName + L"\r\n";
    text += L"  Disk: " + settings.diskDisplayName + L"\r\n";
    text += L"  Refresh interval: " + settings.refreshInterval + L"\r\n";
    text += L"  Background refresh: " + settings.backgroundRefresh + L"\r\n";
    text += L"  Opacity: " + settings.opacity + L"\r\n";
    text += L"  High usage alerts: " + std::wstring(settings.highUsageAlerts ? L"On" : L"Off") +
            L" (" + settings.alertThreshold + L")\r\n";
    text += L"  Always on top: " + std::wstring(settings.alwaysOnTop ? L"On" : L"Off") + L"\r\n";
    text += L"  Position locked: " + std::wstring(settings.lockPosition ? L"On" : L"Off") + L"\r\n";
    text += L"  Global hotkey: " + std::wstring(settings.globalHotkeyRegistered ? L"Registered" : L"Off") + L"\r\n";
    text += L"\r\nGenerated: " + formatCurrentClock() + L"\r\n";
    return text;
}
