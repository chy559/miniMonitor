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

#include "app_paths.h"

#include <windows.h>

#include <cstdio>

std::wstring executablePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) {
        return L"";
    }
    while (length >= path.size()) {
        path.assign(path.size() * 2, L'\0');
        length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return L"";
        }
    }
    path.resize(length);
    return path;
}

std::wstring appDirectory() {
    std::wstring path = executablePath();
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }
    return path.substr(0, slash);
}

std::wstring reportsDirectory() {
    std::wstring folder = appDirectory();
    if (folder.empty()) {
        return L"";
    }
    return folder + L"\\reports";
}

bool ensureReportsDirectory(std::wstring& folder) {
    folder = reportsDirectory();
    if (folder.empty()) {
        return false;
    }
    if (CreateDirectoryW(folder.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    return false;
}

std::wstring reportTimestampForFile() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[40]{};
    swprintf(buffer, 40, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay,
             time.wHour, time.wMinute, time.wSecond);
    return buffer;
}
