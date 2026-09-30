// SPDX-License-Identifier: GPL-3.0-or-later
//
// Result composition and the copy / save / print actions behind the toolbar buttons.
#pragma once

#include "drawtools.h"
#include "screengrabber.h"

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

class QWidget;

namespace ls {

// The selected pixels with the annotations drawn on top, at the scale of the screen the
// selection starts on.
QImage composeResultImage(const QVector<ScreenShot>& shots, const QRect& canvasSelection,
                          qreal dpr, const QVector<DrawObjectPtr>& objects);

// ~/Pictures/Tinyshot, where saves go until the user picks another folder.
QString defaultSaveDirectory();
// The last folder saved to, or the default above. Created if missing.
QString saveDirectory();
QString ensureDirectory(const QString& path);

// True for the document-portal paths a sandboxed file chooser returns
// ($XDG_RUNTIME_DIR/doc/<id>/...): that directory stands for a single granted file, so new
// screenshots cannot be saved there and the granted file name must not be changed.
bool isPortalExportPath(const QString& path);

// The folder the next save uses after a save dialog picked `path`: its folder, or an empty
// string (the default ~/Pictures/Tinyshot) when the dialog ran in a sandbox.
QString savedDirectoryFor(const QString& path);

// First free "Screenshot_N" name in `dir`, counting from 1 up to 9999.
QString uniqueScreenshotPath(const QString& dir, int format);

// Format codes as stored in the settings: 1 = PNG, 2 = JPEG, 3 = BMP.
QString saveExtension(int format);
int formatFromExtension(const QString& path);

bool copyImageToClipboard(const QImage& image, QString* error);

bool saveImageToFile(const QImage& image, const QString& path, int format, int jpegQuality,
                     QString* error);

// Shows the print dialog, then scales the image to fit the page, keeping its aspect ratio.
bool printImage(QWidget* parent, const QImage& image, QString* error);

} // namespace ls
