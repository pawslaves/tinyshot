// SPDX-License-Identifier: GPL-3.0-or-later

#include "captureactions.h"

#include "app/settings.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace ls {

QImage composeResultImage(const QVector<ScreenShot>& shots, const QRect& canvasSelection,
                          qreal dpr, const QVector<DrawObjectPtr>& objects)
{
  if (canvasSelection.isEmpty())
    return QImage();
  const qreal scale = dpr > 0 ? dpr : 1.0;
  const QSize pixelSize(qRound(canvasSelection.width() * scale),
                        qRound(canvasSelection.height() * scale));
  if (pixelSize.isEmpty())
    return QImage();

  QImage out(pixelSize, QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::black);

  QPainter painter(&out);
  for (const ScreenShot& shot : shots) {
    if (shot.image.isNull())
      continue;
    const QRect part = shot.canvasRect.intersected(canvasSelection);
    if (part.isEmpty())
      continue;
    // Source rectangle in this screen's device pixels.
    const QRectF src(part.topLeft() - shot.canvasRect.topLeft(), QSizeF(part.size()));
    const QRectF srcDev(src.x() * shot.dpr, src.y() * shot.dpr, src.width() * shot.dpr,
                        src.height() * shot.dpr);
    const QRectF target(QPointF(part.topLeft() - canvasSelection.topLeft()) * scale,
                        QSizeF(part.width() * scale, part.height() * scale));
    painter.drawImage(target, shot.image, srcDev);
  }

  // Annotations are stored in desktop coordinates. Move the selection's corner to the origin,
  // then scale to device pixels. QPainter applies transforms in reverse call order, so scale()
  // has to come first here.
  painter.scale(scale, scale);
  painter.translate(-canvasSelection.topLeft());
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const DrawObjectPtr& object : objects) {
    if (object)
      object->draw(painter);
  }
  painter.end();
  return out.convertToFormat(QImage::Format_ARGB32);
}

QString defaultSaveDirectory()
{
  // Saves go under Pictures.
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const QString base = pictures.isEmpty() ? QDir::homePath() : pictures;
  return base + QStringLiteral("/Tinyshot");
}

QString ensureDirectory(const QString& path)
{
  if (path.isEmpty())
    return path;
  QDir dir(path);
  if (!dir.exists())
    dir.mkpath(QStringLiteral("."));
  return path;
}

QString saveDirectory()
{
  const QString configured = Settings::instance().lastSavedDir();
  if (!configured.isEmpty() && QDir(configured).exists())
    return configured;
  return ensureDirectory(defaultSaveDirectory());
}

bool isPortalExportPath(const QString& path)
{
  // Inside a sandbox the file chooser portal returns the chosen file through the document
  // portal, which lives under $XDG_RUNTIME_DIR/doc/.
  const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  return !runtime.isEmpty() && path.startsWith(runtime + QStringLiteral("/doc/"));
}

QString savedDirectoryFor(const QString& path)
{
  const QString directory = QFileInfo(path).absolutePath();
  if (isPortalExportPath(directory))
    return QString();
  return directory;
}

QString saveExtension(int format)
{
  switch (format) {
  case 2:
    return QStringLiteral("jpg");
  case 3:
    return QStringLiteral("bmp");
  case 1:
  default:
    return QStringLiteral("png");
  }
}

int formatFromExtension(const QString& path)
{
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
    return 2;
  if (suffix == QLatin1String("bmp"))
    return 3;
  return 1;
}

QString uniqueScreenshotPath(const QString& dir, int format)
{
  // "Screenshot_1" and, while the name is taken, "Screenshot_2" ... "Screenshot_9999".
  const QString ext = saveExtension(format);
  QString name = QDir(dir).filePath(QStringLiteral("Screenshot_1.%1").arg(ext));
  int counter = 2;
  while (QFileInfo::exists(name) && counter <= 9999) {
    name = QDir(dir).filePath(QStringLiteral("Screenshot_%1.%2").arg(counter).arg(ext));
    ++counter;
  }
  return name;
}

bool copyImageToClipboard(const QImage& image, QString* error)
{
  if (image.isNull()) {
    if (error)
      *error = QObject::tr("Nothing to copy.");
    return false;
  }
  QClipboard* clipboard = QGuiApplication::clipboard();
  if (!clipboard) {
    if (error)
      *error = QObject::tr("No clipboard available.");
    return false;
  }
  clipboard->setImage(image);
  return true;
}

bool saveImageToFile(const QImage& image, const QString& path, int format, int jpegQuality,
                     QString* error)
{
  if (image.isNull()) {
    if (error)
      *error = QObject::tr("Nothing to save.");
    return false;
  }
  const QByteArray suffix = saveExtension(format).toLatin1();
  QImageWriter writer(path, suffix);
  if (format == 2)
    writer.setQuality(std::clamp(jpegQuality, 50, 100));  // stored range is 50..100
  if (!writer.write(image)) {
    if (error)
      *error = writer.errorString();
    return false;
  }
  return true;
}

bool printImage(QWidget* parent, const QImage& image, QString* error)
{
  if (image.isNull()) {
    if (error)
      *error = QObject::tr("Nothing to print.");
    return false;
  }
  QPrinter printer(QPrinter::HighResolution);
  printer.setDocName(QStringLiteral("Screenshot"));
  QPrintDialog dialog(&printer, parent);
  dialog.setWindowTitle(QObject::tr("Print"));
  if (dialog.exec() != QDialog::Accepted)
    return false;

  QPainter painter;
  if (!painter.begin(&printer)) {
    if (error)
      *error = QObject::tr("Cannot start printing.");
    return false;
  }

  // Fit the image to the printable area, keeping its aspect ratio.
  const QRectF page = printer.pageLayout().paintRectPixels(printer.resolution());
  const QSizeF target = QSizeF(image.size()).scaled(page.size(), Qt::KeepAspectRatio);
  const QRectF targetRect(QPointF(page.left() + (page.width() - target.width()) / 2.0,
                                  page.top() + (page.height() - target.height()) / 2.0),
                          target);
  painter.drawImage(targetRect, image);
  painter.end();
  return true;
}

} // namespace ls
