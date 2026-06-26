#pragma once

#include "app_models.h"

#include <gdiplus.h>

struct ThemePalette {
    const wchar_t* name;
    Gdiplus::Color bgTop;
    Gdiplus::Color bgBottom;
    Gdiplus::Color panel;
    Gdiplus::Color panelBorder;
    Gdiplus::Color panelEdge;
    Gdiplus::Color text;
    Gdiplus::Color muted;
    Gdiplus::Color subtle;
    Gdiplus::Color iconBg;
    Gdiplus::Color iconFg;
    Gdiplus::Color accent;
    Gdiplus::Color accent2;
    Gdiplus::Color accent3;
    Gdiplus::Color barBg;
    Gdiplus::Color buttonBg;
    Gdiplus::Color sparkArea;
    Gdiplus::Color frameLight;
    Gdiplus::Color frameDark;
};

ThemePalette paletteForTheme(UiTheme theme);
