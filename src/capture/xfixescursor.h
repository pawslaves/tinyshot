// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cursor image for the screenshot overlay, taken from XFixes on X11.  The portal backend used on
// Wayland has no equivalent, so the CaptureCursor option is not honoured there.
#pragma once

#include <QImage>
#include <QPoint>

#include <optional>

namespace ls {

struct CursorImage {
  QImage image;      // ARGB32_Premultiplied cursor pixels (XFixes order)
  QPoint hotspot;    // hotspot inside the cursor image
  QPoint position;   // cursor position in X root coordinates == device pixels
};

// True when the translation unit was compiled with X11/XFixes headers available.
bool x11CursorSupportCompiled();

// Current cursor image/position, or std::nullopt when XFixes is unavailable (Wayland session,
// no DISPLAY, no libXfixes) - callers then skip the cursor overlay.
std::optional<CursorImage> queryX11CursorImage();

} // namespace ls
