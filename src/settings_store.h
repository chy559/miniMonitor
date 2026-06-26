#pragma once

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

#include <string>

class SettingsStore {
public:
    bool readBool(const wchar_t* name, bool fallback) const;
    void writeBool(const wchar_t* name, bool value) const;

    DWORD readDword(const wchar_t* name, DWORD fallback) const;
    void writeDword(const wchar_t* name, DWORD value) const;

    bool readWindowPosition(int& x, int& y) const;
    void writeWindowPosition(int x, int y) const;
    void clear() const;

    bool isAutoStartEnabled() const;
    bool setAutoStart(bool enabled, const std::wstring& command) const;
};
