#include "format_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

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

std::wstring trimForTip(const std::wstring& text) {
    constexpr size_t kMaxTipChars = 127;
    if (text.size() <= kMaxTipChars) {
        return text;
    }
    return text.substr(0, kMaxTipChars - 3) + L"...";
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

std::wstring formatCurrentClock() {
    std::time_t nowTime = std::time(nullptr);
    std::tm nowLocal{};
    if (localtime_s(&nowLocal, &nowTime) != 0) {
        return L"Updated";
    }

    wchar_t buffer[32]{};
    swprintf(buffer, 32, L"Updated %02d:%02d:%02d", nowLocal.tm_hour, nowLocal.tm_min, nowLocal.tm_sec);
    return buffer;
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
