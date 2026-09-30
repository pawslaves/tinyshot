// SPDX-License-Identifier: GPL-3.0-or-later

#include "xfixescursor.h"

#include <QLibrary>
#include <QLoggingCategory>

// LS_HAVE_X11 is defined by the build when the X11 development files were found; XFixes itself
// is resolved with QLibrary at runtime, so no extra link dependency is needed.
#if defined(LS_HAVE_X11)
#  if __has_include(<X11/Xlib.h>) && __has_include(<X11/extensions/Xfixes.h>)
#    define LS_HAVE_X11_XFIXES 1
#  endif
#endif

#ifdef LS_HAVE_X11_XFIXES
#  include <X11/Xlib.h>
#  include <X11/extensions/Xfixes.h>
#endif

namespace ls {

bool x11CursorSupportCompiled()
{
#ifdef LS_HAVE_X11_XFIXES
  return true;
#else
  return false;
#endif
}

#ifdef LS_HAVE_X11_XFIXES

namespace {

using XOpenDisplayFn = Display* (*)(const char*);
using XCloseDisplayFn = int (*)(Display*);
using XFreeFn = int (*)(void*);
using XFixesGetCursorImageFn = XFixesCursorImage* (*)(Display*);

struct X11Libs {
  QLibrary x11{QLatin1String("libX11.so.6")};
  QLibrary xfixes{QLatin1String("libXfixes.so.3")};
  XOpenDisplayFn openDisplay = nullptr;
  XCloseDisplayFn closeDisplay = nullptr;
  XFreeFn xfree = nullptr;
  XFixesGetCursorImageFn getCursorImage = nullptr;
  bool resolved = false;
  bool ok = false;

  void resolve()
  {
    if (resolved)
      return;
    resolved = true;
    if (x11.load()) {
      openDisplay = reinterpret_cast<XOpenDisplayFn>(x11.resolve("XOpenDisplay"));
      closeDisplay = reinterpret_cast<XCloseDisplayFn>(x11.resolve("XCloseDisplay"));
      xfree = reinterpret_cast<XFreeFn>(x11.resolve("XFree"));
    }
    if (xfixes.load())
      getCursorImage =
          reinterpret_cast<XFixesGetCursorImageFn>(xfixes.resolve("XFixesGetCursorImage"));
    ok = openDisplay && closeDisplay && xfree && getCursorImage;
  }
};

X11Libs& x11Libs()
{
  static X11Libs libs;
  return libs;
}

} // namespace

std::optional<CursorImage> queryX11CursorImage()
{
  X11Libs& libs = x11Libs();
  libs.resolve();
  if (!libs.ok)
    return std::nullopt;

  Display* display = libs.openDisplay(nullptr);
  if (!display)
    return std::nullopt;
  XFixesCursorImage* raw = libs.getCursorImage(display);
  if (!raw) {
    libs.closeDisplay(display);
    return std::nullopt;
  }

  // XFixes stores one `unsigned long` per pixel; only the low 32 bits carry the ARGB value, so
  // the buffer is not a valid raw ARGB32 image on 64-bit platforms - copy pixel by pixel.
  CursorImage result;
  const int w = raw->width;
  const int h = raw->height;
  result.image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < h; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(result.image.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const unsigned long pixel = raw->pixels[y * w + x];
      line[x] = static_cast<QRgb>(pixel & 0xffffffffUL);
    }
  }
  result.hotspot = QPoint(raw->xhot, raw->yhot);
  result.position = QPoint(raw->x, raw->y);

  libs.xfree(raw);
  libs.closeDisplay(display);
  return result;
}

#else // !LS_HAVE_X11_XFIXES

std::optional<CursorImage> queryX11CursorImage()
{
  // Built without X11/XFixes headers: the cursor overlay is skipped.
  return std::nullopt;
}

#endif

} // namespace ls
