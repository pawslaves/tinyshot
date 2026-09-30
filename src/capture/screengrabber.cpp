// SPDX-License-Identifier: GPL-3.0-or-later

#include "screengrabber.h"

#include "xfixescursor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include "app/settings.h"

namespace ls {

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kPortalInterface = "org.freedesktop.portal.Screenshot";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

// How long to wait for the portal before giving up. The compositor normally answers almost
// instantly; this only catches a stuck portal.
constexpr int kPortalTimeoutMs = 30000;

QRect deviceRectOfScreen(QScreen* screen, qreal dpr)
{
  const QRect g = screen->geometry();
  return QRect(QPoint(qRound(g.x() * dpr), qRound(g.y() * dpr)),
               QSize(qRound(g.width() * dpr), qRound(g.height() * dpr)));
}

} // namespace

ScreenGrabber::ScreenGrabber(QObject* parent) : QObject(parent)
{
  m_usePortal = usePortal();
  m_timeout = new QTimer(this);
  m_timeout->setSingleShot(true);
  connect(m_timeout, &QTimer::timeout, this, &ScreenGrabber::onPortalTimeout);
}

ScreenGrabber::~ScreenGrabber() = default;

bool ScreenGrabber::usePortal()
{
  const QString platform = QGuiApplication::platformName().toLower();
  if (platform.contains(QLatin1String("wayland")))
    return true;
  if (platform.contains(QLatin1String("xcb")) || platform.contains(QLatin1String("x11")))
    return false;
  // Anything else (offscreen, minimal, unknown plugins): use the portal on Wayland sessions,
  // since it's the only backend that works everywhere.
  const QByteArray session = qgetenv("XDG_SESSION_TYPE");
  return session.toLower().contains("wayland");
}

void ScreenGrabber::grab()
{
  if (m_busy)
    return;
  m_busy = true;
  if (m_usePortal)
    grabViaPortal();
  else
    grabDirect();
}

void ScreenGrabber::finish(const QVector<ScreenShot>& shots)
{
  m_busy = false;
  if (m_timeout)
    m_timeout->stop();
  disconnectPortalMatch();
  emit grabbed(shots);
}

void ScreenGrabber::fail(const QString& error)
{
  m_busy = false;
  if (m_timeout)
    m_timeout->stop();
  disconnectPortalMatch();
  emit failed(error);
}

void ScreenGrabber::disconnectPortalMatch()
{
  if (m_requestPath.isEmpty() && m_connectedPath.isEmpty())
    return;
  QDBusConnection bus = QDBusConnection::sessionBus();
  // Both the predicted and the actually returned request path may have been subscribed; a
  // leftover match would deliver the Response of a timed-out request to the next capture.
  for (const QString& path : {m_requestPath, m_connectedPath}) {
    if (!path.isEmpty())
      bus.disconnect(QString::fromLatin1(kPortalService), path,
                     QString::fromLatin1(kRequestInterface), QStringLiteral("Response"), this,
                     SLOT(onPortalResponse(uint,QVariantMap)));
  }
  m_requestPath.clear();
  m_connectedPath.clear();
}

void ScreenGrabber::grabDirect()
{
  const QList<QScreen*> screens = QGuiApplication::screens();
  QVector<ScreenShot> shots;
  shots.reserve(screens.size());

  for (QScreen* screen : screens) {
    ScreenShot shot;
    shot.screen = screen;
    shot.canvasRect = screen->geometry();
    shot.dpr = screen->devicePixelRatio();
    // X11: one grab per screen; QScreen::grabWindow(0) uses the screen's root window and
    // returns the screen's own pixels (device pixels).
    const QPixmap pixmap = screen->grabWindow(0);
    if (!pixmap.isNull()) {
      shot.image = pixmap.toImage();
      shot.image.setDevicePixelRatio(shot.dpr);
    } else {
      // A screen that cannot be grabbed is left black rather than failing the whole capture.
      shot.image = QImage(deviceRectOfScreen(screen, shot.dpr).size(),
                          QImage::Format_ARGB32_Premultiplied);
      shot.image.fill(Qt::black);
      shot.image.setDevicePixelRatio(shot.dpr);
    }
    shots.append(shot);
  }

  if (shots.isEmpty()) {
    fail(tr("No screens available."));
    return;
  }

  // CaptureCursor (off by default): draw the pointer into the image after the grab.  Portal
  // captures cannot carry a cursor, so the option only has an effect on X11.
  if (Settings::instance().captureCursor()) {
    if (const std::optional<CursorImage> cursor = queryX11CursorImage()) {
      for (ScreenShot& shot : shots) {
        if (shot.image.isNull() || !shot.screen)
          continue;
        const QPoint origin(qRound(shot.canvasRect.x() * shot.dpr),
                            qRound(shot.canvasRect.y() * shot.dpr));
        const QPoint local = cursor->position - origin;
        const QPoint topLeft = local - cursor->hotspot;
        if (!QRect(QPoint(0, 0), shot.image.size()).intersects(
                QRect(topLeft, cursor->image.size())))
          continue;
        QPainter painter(&shot.image);
        painter.drawImage(topLeft, cursor->image);
      }
    } else if (!x11CursorSupportCompiled()) {
      // Built without X11/XFixes support: the cursor overlay is skipped.
      qWarning("tinyshot: CaptureCursor requested but this build has no X11/XFixes support");
    }
  }

  finish(shots);
}

void ScreenGrabber::grabViaPortal()
{
  // Request object paths are derived from the caller's unique bus name and the handle token we
  // pass in the options, so the Response signal can be connected before the call is made.
  const QString unique = QDBusConnection::sessionBus().baseService();
  QString senderToken = unique;
  senderToken.remove(QLatin1Char(':'));
  senderToken.replace(QLatin1Char('.'), QLatin1Char('_'));
  static quint64 counter = 0;
  m_token = QStringLiteral("tinyshot%1").arg(++counter);
  m_requestPath =
      QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(senderToken, m_token);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    fail(tr("No D-Bus session bus available."));
    return;
  }
  bus.connect(QString::fromLatin1(kPortalService), m_requestPath,
              QString::fromLatin1(kRequestInterface), QStringLiteral("Response"), this,
              SLOT(onPortalResponse(uint,QVariantMap)));
  m_connectedPath = m_requestPath;
  startPortalCall();
}

void ScreenGrabber::startPortalCall()
{
  QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                       QString::fromLatin1(kPortalPath),
                                                       QString::fromLatin1(kPortalInterface),
                                                       QStringLiteral("Screenshot"));
  // No parent window handle is passed: there is no exported X11 window id to hand over, and
  // interactive=false never shows a dialog that would need one.
  message << QString();
  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), m_token);
  options.insert(QStringLiteral("interactive"), false);
  message << options;

  QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(message);
  auto* watcher = new QDBusPendingCallWatcher(pending, this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError()) {
              fail(tr("The screenshot portal is not available: %1").arg(reply.error().message()));
              return;
            }
            const QString path = reply.value().path();
            if (!path.isEmpty() && path != m_connectedPath) {
              QDBusConnection::sessionBus().connect(
                  QString::fromLatin1(kPortalService), path,
                  QString::fromLatin1(kRequestInterface), QStringLiteral("Response"), this,
                  SLOT(onPortalResponse(uint,QVariantMap)));
              m_connectedPath = path;
            }
            beginPortalWait();
          });
}

void ScreenGrabber::beginPortalWait()
{
  if (m_timeout)
    m_timeout->start(kPortalTimeoutMs);
}

void ScreenGrabber::onPortalTimeout()
{
  if (!m_busy)
    return;
  fail(tr("The screenshot portal did not answer in time."));
}

void ScreenGrabber::onPortalResponse(uint response, const QVariantMap& results)
{
  if (!m_busy)
    return;
  m_timeout->stop();
  if (response != 0) {
    // 1 = the user cancelled, 2 = the request failed (portal Screenshot spec).
    fail(response == 1 ? tr("Screenshot cancelled.") : tr("The screenshot was not taken."));
    return;
  }
  const QString uri = results.value(QStringLiteral("uri")).toString();
  if (uri.isEmpty()) {
    fail(tr("The screenshot portal returned no image."));
    return;
  }
  const QUrl url(uri);
  const QString path = url.isLocalFile() ? url.toLocalFile() : uri;
  QImage image;
  if (!image.load(path)) {
    fail(tr("Cannot read the screenshot file %1.").arg(path));
    return;
  }
  // The compositor writes the image to a private temp file; clean it up once it is in memory.
  if (!url.isLocalFile() || path.startsWith(QDir::tempPath()))
    QFile::remove(path);
  finish(mapPortalImage(image));
}

QVector<ScreenShot> ScreenGrabber::mapPortalImage(const QImage& image) const
{
  const QList<QScreen*> screens = QGuiApplication::screens();
  QVector<ScreenShot> shots;
  shots.reserve(screens.size());
  if (screens.isEmpty())
    return shots;

  QVector<QRect> deviceRects;
  deviceRects.reserve(screens.size());
  QRect unionDev;
  for (QScreen* screen : screens) {
    const QRect dev = deviceRectOfScreen(screen, screen->devicePixelRatio());
    deviceRects.append(dev);
    unionDev = unionDev.united(dev);
  }

  // The portal returns either the whole desktop, which we split per screen, or a single output,
  // which we match to a screen by size (preferring the one under the pointer). Screens it didn't
  // cover stay black so the layout is the same as with the X11 backend.
  const bool wholeDesktop = (image.size() == unionDev.size());
  int matchedIndex = -1;
  if (!wholeDesktop) {
    QPoint pointerPos;
    bool havePointer = false;
    if (const std::optional<CursorImage> cursor = queryX11CursorImage()) {
      pointerPos = cursor->position;
      havePointer = true;
    }
    for (int i = 0; i < deviceRects.size(); ++i) {
      if (deviceRects.at(i).size() != image.size())
        continue;
      if (matchedIndex < 0)
        matchedIndex = i;
      if (havePointer && deviceRects.at(i).contains(pointerPos)) {
        matchedIndex = i;
        break;
      }
    }
  }

  for (int i = 0; i < screens.size(); ++i) {
    QScreen* screen = screens.at(i);
    const qreal dpr = screen->devicePixelRatio();
    const QRect dev = deviceRects.at(i);

    ScreenShot shot;
    shot.screen = screen;
    shot.canvasRect = screen->geometry();
    shot.dpr = dpr;

    if (wholeDesktop) {
      shot.image = image.copy(dev.translated(-unionDev.topLeft()));
    } else if (i == matchedIndex) {
      shot.image = image;
    } else {
      shot.image = QImage(dev.size(), QImage::Format_ARGB32_Premultiplied);
      shot.image.fill(Qt::black);
    }
    shot.image.setDevicePixelRatio(dpr);
    shots.append(shot);
  }
  return shots;
}

} // namespace ls
