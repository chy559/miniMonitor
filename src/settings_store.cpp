#include "settings_store.h"

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MiniMonitor";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"MiniMonitor";

} // namespace

bool SettingsStore::readBool(const wchar_t* name, bool fallback) const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return fallback;
    }
    DWORD raw = fallback ? 1 : 0;
    DWORD type = REG_DWORD;
    DWORD size = sizeof(DWORD);
    const bool ok = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&raw), &size) == ERROR_SUCCESS &&
                    type == REG_DWORD;
    RegCloseKey(key);
    return ok ? raw != 0 : fallback;
}

void SettingsStore::writeBool(const wchar_t* name, bool value) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    DWORD raw = value ? 1 : 0;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&raw), sizeof(DWORD));
    RegCloseKey(key);
}

DWORD SettingsStore::readDword(const wchar_t* name, DWORD fallback) const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return fallback;
    }
    DWORD raw = fallback;
    DWORD type = REG_DWORD;
    DWORD size = sizeof(DWORD);
    const bool ok = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&raw), &size) == ERROR_SUCCESS &&
                    type == REG_DWORD;
    RegCloseKey(key);
    return ok ? raw : fallback;
}

void SettingsStore::writeDword(const wchar_t* name, DWORD value) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    RegCloseKey(key);
}

bool SettingsStore::readWindowPosition(int& x, int& y) const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD rawX = 0;
    DWORD rawY = 0;
    DWORD type = REG_DWORD;
    DWORD size = sizeof(DWORD);
    const bool okX = RegQueryValueExW(key, L"WindowX", nullptr, &type, reinterpret_cast<BYTE*>(&rawX), &size) == ERROR_SUCCESS &&
                     type == REG_DWORD;
    type = REG_DWORD;
    size = sizeof(DWORD);
    const bool okY = RegQueryValueExW(key, L"WindowY", nullptr, &type, reinterpret_cast<BYTE*>(&rawY), &size) == ERROR_SUCCESS &&
                     type == REG_DWORD;
    RegCloseKey(key);
    x = static_cast<int>(static_cast<LONG>(rawX));
    y = static_cast<int>(static_cast<LONG>(rawY));
    return okX && okY;
}

void SettingsStore::writeWindowPosition(int x, int y) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    DWORD rawX = static_cast<DWORD>(x);
    DWORD rawY = static_cast<DWORD>(y);
    RegSetValueExW(key, L"WindowX", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&rawX), sizeof(DWORD));
    RegSetValueExW(key, L"WindowY", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&rawY), sizeof(DWORD));
    RegCloseKey(key);
}

void SettingsStore::clear() const {
    RegDeleteTreeW(HKEY_CURRENT_USER, kSettingsKey);
}

bool SettingsStore::isAutoStartEnabled() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[2048]{};
    DWORD type = REG_SZ;
    DWORD size = sizeof(buffer);
    const bool ok = RegQueryValueExW(key, kRunValueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) ==
                        ERROR_SUCCESS &&
                    type == REG_SZ && buffer[0] != L'\0';
    RegCloseKey(key);
    return ok;
}

bool SettingsStore::setAutoStart(bool enabled, const std::wstring& command) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        ok = !command.empty() &&
             RegSetValueExW(key, kRunValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LSTATUS status = RegDeleteValueW(key, kRunValueName);
        ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}
