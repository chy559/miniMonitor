#pragma once

#include <string>

std::wstring formatBytes(double bytes);
std::wstring formatSpeed(double bytesPerSecond);
std::wstring formatPercent(double value);
std::wstring formatOneDecimalPercent(double value);
std::wstring trimForTip(const std::wstring& text);
std::wstring formatResetDetail(double value);
std::wstring formatCurrentClock();
std::wstring quotaWindowLabel(double seconds);
