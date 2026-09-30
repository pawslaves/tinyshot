// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs a screenshot command: interactive capture, or the instant save / upload of the whole
// screen.
#pragma once

#include "screengrabber.h"

#include <QImage>
#include <QObject>
#include <QRect>

namespace ls {

class CaptureSession;
class ScreenGrabber;
class Uploader;
class UploadProgressDialog;

class CaptureController : public QObject {
  Q_OBJECT

public:
  explicit CaptureController(QObject* parent = nullptr);

  // command: 0 = interactive capture, 1 = instant save of the full screen, 2 = instant upload.
  void makeScreenshot(int command);

signals:
  // Ask the tray application to show a balloon notification / open the options dialog.
  void balloon(const QString& title, const QString& text, const QString& url);
  void optionsRequested();

private slots:
  void onGrabbed(const QVector<ls::ScreenShot>& shots);
  void onGrabFailed(const QString& error);
  void onSessionFinished();
  void onUploadRequested(const QImage& image, int shareMode, qreal dpr);

private:
  void saveFullScreen(const QVector<ScreenShot>& shots);
  void uploadFullScreen(const QVector<ScreenShot>& shots);
  void startInteractive(const QVector<ScreenShot>& shots);
  void reportError(const QString& text);

  ScreenGrabber* m_grabber = nullptr;
  Uploader* m_uploader = nullptr;
  UploadProgressDialog* m_uploadDialog = nullptr;  // self-deleting on close (WA_DeleteOnClose)
  CaptureSession* m_session = nullptr;
  int m_pendingCommand = 0;
  bool m_busy = false;
  QRect m_lastSelection;  // the selection remembered for the next capture
};

} // namespace ls
