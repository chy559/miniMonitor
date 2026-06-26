#pragma once

#include <windows.h>

#include <string>

std::wstring computerName();
void enableDpiAwareness();
UINT systemDpi();
HICON loadAppIcon(HINSTANCE instance);
