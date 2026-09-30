// SPDX-License-Identifier: GPL-3.0-or-later

#include "capturecontroller.h"

#include "app/settings.h"
#include "captureactions.h"
#include "capturesession.h"
#include "net/uploader.h"
#include "screengrabber.h"
#include "uploadprogressdialog.h"

#include <QFileInfo>
#include <QImage>
#include <QPointer>

namespace ls {

namespace {

QString balloonTitle()
{
  return QStringLiteral("Tinyshot");
}

} // namespace

CaptureController::CaptureController(QObject* parent) : QObject(parent) {}

void CaptureController::makeScreenshot(int command)
{
  if (command == 4) {
    // Command 4 is the tray protocol's request to open the options dialog.
    emit optionsRequested();
    return;
  }
  // Command 2 is the instant upload of the whole screen; the upload service is opt-in, so the
  // command does nothing while it is off.
  if (command == 2 && !Settings::instance().uploadEnabled())
    return;
  // One capture at a time: further commands are ignored while one is running.
  if (m_busy)
    return;

  m_busy = true;
  m_pendingCommand = command;
  if (!m_grabber) {
    m_grabber = new ScreenGrabber(this);
    connect(m_grabber, &ScreenGrabber::grabbed, this, &CaptureController::onGrabbed);
    connect(m_grabber, &ScreenGrabber::failed, this, &CaptureController::onGrabFailed);
  }
  m_grabber->grab();
}

void CaptureController::onGrabFailed(const QString& error)
{
  m_busy = false;
  reportError(error);
}

void CaptureController::onGrabbed(const QVector<ScreenShot>& shots)
{
  if (shots.isEmpty()) {
    m_busy = false;
    reportError(tr("The screen could not be captured."));
    return;
  }
  switch (m_pendingCommand) {
  case 1:
    saveFullScreen(shots);
    break;
  case 2:
    uploadFullScreen(shots);
    break;
  default:
    startInteractive(shots);
    break;
  }
}

void CaptureController::reportError(const QString& text)
{
  emit balloon(tr("Error"), text, QString());
}

void CaptureController::saveFullScreen(const QVector<ScreenShot>& shots)
{
  // Whole virtual screen as the selection; save straight to the last-used folder, no Save As
  // dialog.
  QRect canvas;
  for (const ScreenShot& shot : shots)
    canvas = canvas.united(shot.canvasRect);
  const qreal dpr = qMax<qreal>(1.0, shots.first().dpr);
  const QImage image = composeResultImage(shots, canvas, dpr, {});
  m_busy = false;

  const int format = Settings::instance().format();
  const QString path = uniqueScreenshotPath(saveDirectory(), format);
  QString error;
  if (!saveImageToFile(image, path, format, Settings::instance().jpegQuality(), &error)) {
    reportError(error.isEmpty() ? tr("Cannot save the screenshot.") : error);
    return;
  }
  if (Settings::instance().showBubbles()) {
    const QString text =
        tr("Screenshot is saved to %1. Click here to open in the folder.")
            .arg(QFileInfo(path).fileName());
    emit balloon(balloonTitle(), text, QFileInfo(path).absolutePath());
  }
}

void CaptureController::uploadFullScreen(const QVector<ScreenShot>& shots)
{
  // Whole virtual screen as the selection; ShareMode::Upload is a plain upload, no share target.
  QRect canvas;
  for (const ScreenShot& shot : shots)
    canvas = canvas.united(shot.canvasRect);
  const qreal dpr = qMax<qreal>(1.0, shots.first().dpr);
  const QImage image = composeResultImage(shots, canvas, dpr, {});
  m_busy = false;
  onUploadRequested(image, static_cast<int>(ShareMode::Upload), dpr);
}

void CaptureController::startInteractive(const QVector<ScreenShot>& shots)
{
  m_session = new CaptureSession(shots, this);
  connect(m_session, &CaptureSession::balloon, this, &CaptureController::balloon);
  connect(m_session, &CaptureSession::finished, this, &CaptureController::onSessionFinished);
  connect(m_session, &CaptureSession::uploadRequested, this, &CaptureController::onUploadRequested);
  const bool keep = Settings::instance().keepSelection();
  m_session->start(keep, m_lastSelection);
}

void CaptureController::onSessionFinished()
{
  if (m_session) {
    // Remember the area for the next capture.
    if (m_session->hasSelection())
      m_lastSelection = m_session->selection();
    m_session->deleteLater();
    m_session = nullptr;
  }
  m_busy = false;
}

void CaptureController::onUploadRequested(const QImage& image, int shareMode, qreal dpr)
{
  // The Uploader has no UI of its own; the dialog shows progress and the result.
  if (!m_uploader)
    m_uploader = new Uploader(this);

  if (m_uploadDialog) {
    // Starting a new upload cancels the old one; close its dialog so only one is listening.
    m_uploadDialog->close();
    m_uploadDialog = nullptr;
  }

  auto* dialog = new UploadProgressDialog(m_uploader);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_uploadDialog = dialog;
  // The old dialog is only deleted later, so make sure its destroyed() doesn't clear the new one.
  connect(dialog, &QObject::destroyed, this, [this, dialog]() {
    if (m_uploadDialog == dialog)
      m_uploadDialog = nullptr;
  });
  connect(dialog, &UploadProgressDialog::balloon, this, &CaptureController::balloon);
  dialog->show();
  dialog->start(image, shareMode, dpr);
}

} // namespace ls
