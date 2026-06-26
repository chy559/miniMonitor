#include "ui_theme.h"

namespace {

Gdiplus::Color colorFromHex(BYTE r, BYTE g, BYTE b, BYTE a = 255) {
    return Gdiplus::Color(a, r, g, b);
}

} // namespace

ThemePalette paletteForTheme(UiTheme theme) {
    switch (theme) {
    case UiTheme::Ocean:
        return {
            L"海盐蓝",
            colorFromHex(232, 245, 249),
            colorFromHex(210, 228, 237),
            colorFromHex(248, 253, 255),
            colorFromHex(255, 255, 255),
            Gdiplus::Color(64, 28, 90, 112),
            colorFromHex(18, 40, 54),
            colorFromHex(76, 104, 120),
            colorFromHex(112, 139, 153),
            colorFromHex(30, 105, 148),
            colorFromHex(240, 250, 255),
            colorFromHex(20, 117, 170),
            colorFromHex(72, 159, 181),
            colorFromHex(246, 167, 76),
            colorFromHex(214, 234, 241),
            colorFromHex(239, 248, 251),
            Gdiplus::Color(30, 20, 117, 170),
            Gdiplus::Color(210, 255, 255, 255),
            Gdiplus::Color(72, 28, 86, 112),
        };
    case UiTheme::Sakura:
        return {
            L"樱雾粉",
            colorFromHex(252, 238, 244),
            colorFromHex(235, 221, 236),
            colorFromHex(255, 250, 253),
            colorFromHex(255, 255, 255),
            Gdiplus::Color(66, 116, 70, 96),
            colorFromHex(58, 35, 55),
            colorFromHex(116, 82, 108),
            colorFromHex(151, 111, 141),
            colorFromHex(177, 80, 126),
            colorFromHex(255, 245, 250),
            colorFromHex(207, 82, 136),
            colorFromHex(118, 105, 190),
            colorFromHex(226, 150, 82),
            colorFromHex(239, 224, 235),
            colorFromHex(255, 246, 251),
            Gdiplus::Color(30, 207, 82, 136),
            Gdiplus::Color(214, 255, 255, 255),
            Gdiplus::Color(74, 120, 72, 98),
        };
    case UiTheme::Forest:
        return {
            L"森林绿",
            colorFromHex(234, 243, 235),
            colorFromHex(214, 229, 216),
            colorFromHex(249, 253, 247),
            colorFromHex(255, 255, 255),
            Gdiplus::Color(70, 56, 91, 64),
            colorFromHex(28, 49, 35),
            colorFromHex(82, 105, 86),
            colorFromHex(117, 137, 119),
            colorFromHex(59, 122, 83),
            colorFromHex(246, 253, 247),
            colorFromHex(55, 132, 87),
            colorFromHex(103, 157, 82),
            colorFromHex(210, 152, 65),
            colorFromHex(222, 235, 220),
            colorFromHex(242, 250, 242),
            Gdiplus::Color(30, 55, 132, 87),
            Gdiplus::Color(210, 255, 255, 255),
            Gdiplus::Color(76, 54, 92, 61),
        };
    case UiTheme::Mono:
    default:
        return {
            L"黑白简约",
            colorFromHex(250, 250, 249),
            colorFromHex(232, 232, 230),
            colorFromHex(255, 255, 255),
            colorFromHex(255, 255, 255),
            Gdiplus::Color(72, 0, 0, 0),
            colorFromHex(16, 16, 16),
            colorFromHex(82, 82, 82),
            colorFromHex(120, 120, 120),
            colorFromHex(24, 24, 24),
            colorFromHex(255, 255, 255),
            colorFromHex(24, 24, 24),
            colorFromHex(96, 96, 96),
            colorFromHex(48, 48, 48),
            colorFromHex(229, 229, 229),
            colorFromHex(248, 248, 248),
            Gdiplus::Color(24, 0, 0, 0),
            Gdiplus::Color(190, 255, 255, 255),
            Gdiplus::Color(72, 0, 0, 0),
        };
    }
}
