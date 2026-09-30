// SPDX-License-Identifier: GPL-3.0-or-later
//
// One interactive capture: the frozen screens, one overlay per screen sharing a single selection
// model and annotation state, and the three toolbars.  A session is created per capture, so the
// annotation state always starts empty.
#pragma once

#include "captureactions.h"
#include "drawstate.h"
#include "selectionmodel.h"
#include "screengrabber.h"

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>

namespace ls {

class OverlayWindow;
class ScreenshotToolbar;
class EditorToolbar;
class ShareToolbar;

class CaptureSession : public QObject {
  Q_OBJECT

public:
  explicit CaptureSession(const QVector<ScreenShot>& shots, QObject* parent = nullptr);
  ~CaptureSession() override;

  // Shows the overlays; with keepSelection a previous selection is restored.
  void start(bool keepSelection, const QRect& lastSelection);

  bool hasSelection() const { return m_selection->hasSelection(); }
  QRect selection() const { return m_selection->rect(); }

  // Closes the capture UI.
  void finish();

signals:
  void balloon(const QString& title, const QString& text, const QString& url);
  // Compose-and-upload request; the receiver owns the uploader so that it outlives the UI.
  void uploadRequested(const QImage& image, int shareMode, qreal dpr);
  void finished();

private slots:
  void onCopyRequested();
  void onSaveRequested(bool instant);
  void onPrintRequested();
  void onUploadActionRequested(int shareMode);
  void onCloseRequested();
  void onSelectionChanged();

private:
  QImage composeResult() const;
  qreal selectionDpr() const;
  OverlayWindow* overlayFor(const QPoint& canvasPos) const;
  OverlayWindow* primaryOverlay() const;
  void updateActiveOverlay();
  void commitPendingEdits();
  void refreshViews();

  QVector<ScreenShot> m_shots;
  SelectionModel* m_selection = nullptr;
  DrawState* m_drawState = nullptr;
  QVector<OverlayWindow*> m_overlays;
  ScreenshotToolbar* m_screenshotBar = nullptr;
  EditorToolbar* m_editorBar = nullptr;
  ShareToolbar* m_shareBar = nullptr;
  OverlayWindow* m_activeOverlay = nullptr;
  bool m_finished = false;
};

} // namespace ls
