// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screen capture front end: one xdg-desktop-portal request on Wayland, one QScreen::grabWindow(0)
// per screen on X11.  A screen the backend cannot deliver is left black rather than failing the
// whole capture; only a total failure is reported.
#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>

class QScreen;
class QTimer;

namespace ls {

struct ScreenShot {
  QScreen* screen = nullptr;  // null only for synthetic shots
  QRect canvasRect;           // this screen's rectangle, in canvas coordinates
  QImage image;               // device pixels, devicePixelRatio already applied
  qreal dpr = 1.0;
};

class ScreenGrabber : public QObject {
  Q_OBJECT

public:
  explicit ScreenGrabber(QObject* parent = nullptr);
  ~ScreenGrabber() override;

  // Starts a capture; ignored while another one is running.
  void grab();

  // Wayland sessions use the portal; every other platform uses the direct grab.
  static bool usePortal();

signals:
  void grabbed(const QVector<ls::ScreenShot>& shots);
  void failed(const QString& error);

private:
  void grabDirect();
  void grabViaPortal();
  void startPortalCall();
  void beginPortalWait();
  void finish(const QVector<ScreenShot>& shots);
  void fail(const QString& error);
  void disconnectPortalMatch();

  QVector<ScreenShot> mapPortalImage(const QImage& image) const;

private slots:
  void onPortalResponse(uint response, const QVariantMap& results);
  void onPortalTimeout();

private:
  bool m_busy = false;
  bool m_usePortal = false;
  QString m_token;
  QString m_requestPath;
  QString m_connectedPath;
  QTimer* m_timeout = nullptr;
};

} // namespace ls
