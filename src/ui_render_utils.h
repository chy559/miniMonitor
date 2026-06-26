#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <memory>

std::unique_ptr<Gdiplus::GraphicsPath> roundedRect(Gdiplus::RectF rect, Gdiplus::REAL radius);
