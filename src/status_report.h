#pragma once

#include "app_models.h"

#include <string>

struct StatusReportSettings {
    std::wstring themeName;
    std::wstring diskDisplayName;
    std::wstring refreshInterval;
    std::wstring backgroundRefresh;
    std::wstring opacity;
    std::wstring alertThreshold;
    bool highUsageAlerts = false;
    bool alwaysOnTop = false;
    bool lockPosition = false;
    bool globalHotkeyRegistered = false;
};

std::wstring buildStatusSummaryText(const Metrics& metrics, const std::wstring& diskDisplayName);
std::wstring buildStatusReportText(const Metrics& metrics, const StatusReportSettings& settings);
