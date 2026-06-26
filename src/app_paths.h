#pragma once

#include <string>

std::wstring executablePath();
std::wstring appDirectory();
std::wstring reportsDirectory();
bool ensureReportsDirectory(std::wstring& folder);
std::wstring reportTimestampForFile();
