#include "ui_render_utils.h"

std::unique_ptr<Gdiplus::GraphicsPath> roundedRect(Gdiplus::RectF rect, Gdiplus::REAL radius) {
    auto path = std::make_unique<Gdiplus::GraphicsPath>();
    const Gdiplus::REAL d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180, 90);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    path->CloseFigure();
    return path;
}
