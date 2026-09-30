// SPDX-License-Identifier: GPL-3.0-or-later

#include "capturesession.h"

#include "app/settings.h"
#include "overlaywindow.h"
#include "toolbars.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

namespace ls {

namespace {

// Balloon title for every capture notification.
QString balloonTitle()
{
  return QStringLiteral("Tinyshot");
}

} // namespace

CaptureSession::CaptureSession(const QVector<ScreenShot>& shots, QObject* parent)
    : QObject(parent), m_shots(shots)
{
  m_selection = new SelectionModel(this);
  m_drawState = new DrawState(this);

  QRect canvas;
  for (const ScreenShot& shot : m_shots)
    canvas = canvas.united(shot.canvasRect);
  m_selection->setCanvasBounds(canvas);

  for (const ScreenShot& shot : m_shots) {
    auto* overlay = new OverlayWindow(shot, m_selection, m_drawState);
    connect(overlay, &OverlayWindow::copyRequested, this, &CaptureSession::onCopyRequested);
    connect(overlay, &OverlayWindow::saveRequested, this, &CaptureSession::onSaveRequested);
    connect(overlay, &OverlayWindow::printRequested, this, &CaptureSession::onPrintRequested);
    connect(overlay, &OverlayWindow::uploadRequested, this,
            &CaptureSession::onUploadActionRequested);
    connect(overlay, &OverlayWindow::closeRequested, this, &CaptureSession::onCloseRequested);
    connect(overlay, &OverlayWindow::selectionChanged, this, &CaptureSession::onSelectionChanged);
    m_overlays.append(overlay);
  }

  // The toolbar artwork variant follows the screen scale (>= 150 % -> 2x art).
  const qreal dpr = m_shots.isEmpty() ? 1.0 : qMax<qreal>(1.0, m_shots.first().dpr);
  m_screenshotBar = new ScreenshotToolbar(dpr);
  // The Upload, Share and Google buttons are part of the opt-in upload service.
  m_screenshotBar->setUploadEnabled(Settings::instance().uploadEnabled());
  m_editorBar = new EditorToolbar(dpr);
  m_shareBar = new ShareToolbar(dpr);

  connect(m_screenshotBar, &ScreenshotToolbar::uploadRequested, this,
          [this]() { onUploadActionRequested(0); });
  connect(m_screenshotBar, &ScreenshotToolbar::shareRequested, this, [this]() {
    if (m_activeOverlay)
      m_activeOverlay->showSharePopup();
  });
  connect(m_screenshotBar, &ScreenshotToolbar::googleRequested, this,
          [this]() { onUploadActionRequested(3); });
  connect(m_screenshotBar, &ScreenshotToolbar::printRequested, this,
          &CaptureSession::onPrintRequested);
  connect(m_screenshotBar, &ScreenshotToolbar::copyRequested, this,
          &CaptureSession::onCopyRequested);
  connect(m_screenshotBar, &ScreenshotToolbar::saveRequested, this,
          [this]() { onSaveRequested(false); });
  connect(m_screenshotBar, &ScreenshotToolbar::cancelRequested, this,
          &CaptureSession::onCloseRequested);

  connect(m_editorBar, &EditorToolbar::toolSelected, this, [this](ToolId tool) {
    m_drawState->setTool(tool);
    m_editorBar->setActiveTool(tool);
    refreshViews();
  });
  connect(m_editorBar, &EditorToolbar::colorRequested, this, [this]() {
    commitPendingEdits();
    if (m_activeOverlay)
      m_activeOverlay->openColorPicker();
  });
  connect(m_editorBar, &EditorToolbar::undoRequested, this, [this]() {
    m_drawState->undo();
    refreshViews();
  });

  connect(m_shareBar, &ShareToolbar::shareRequested, this, &CaptureSession::onUploadActionRequested);

  connect(m_drawState, &DrawState::styleChanged, this, [this]() {
    // The tool can change outside the toolbar (Esc deactivates it), so the active button
    // follows the state rather than the button clicks alone.
    m_editorBar->setActiveTool(m_drawState->tool());
    m_editorBar->setColorSwatch(m_drawState->color());
    refreshViews();
  });
  connect(m_drawState, &DrawState::objectsChanged, this, &CaptureSession::refreshViews);

  m_editorBar->setActiveTool(ToolId::None);
  m_editorBar->setColorSwatch(m_drawState->color());
}

CaptureSession::~CaptureSession()
{
  // The toolbars are child widgets of one overlay; delete them before their parent, otherwise
  // they would be destroyed twice.
  delete m_screenshotBar;
  m_screenshotBar = nullptr;
  delete m_editorBar;
  m_editorBar = nullptr;
  delete m_shareBar;
  m_shareBar = nullptr;
  qDeleteAll(m_overlays);
  m_overlays.clear();
}

void CaptureSession::start(bool keepSelection, const QRect& lastSelection)
{
  if (keepSelection && !lastSelection.isEmpty())
    m_selection->setRect(lastSelection);

  // Assign the toolbars to the overlay that owns the selection (or to the primary screen).
  m_activeOverlay = overlayFor(m_selection->hasSelection()
                                   ? m_selection->rect().center()
                                   : (m_shots.isEmpty() ? QPoint() : m_shots.first().canvasRect.center()));
  if (!m_activeOverlay)
    m_activeOverlay = primaryOverlay();

  for (OverlayWindow* overlay : m_overlays)
    overlay->showOverlay();

  if (m_activeOverlay) {
    m_activeOverlay->attachToolbars(m_screenshotBar, m_editorBar, m_shareBar);
    m_activeOverlay->raise();
    m_activeOverlay->activateWindow();
    m_activeOverlay->setFocus(Qt::OtherFocusReason);
  }
  updateActiveOverlay();
  refreshViews();
}

OverlayWindow* CaptureSession::primaryOverlay() const
{
  if (m_overlays.isEmpty())
    return nullptr;
  QScreen* primary = QGuiApplication::primaryScreen();
  for (OverlayWindow* overlay : m_overlays) {
    if (overlay->screen() == primary)
      return overlay;
  }
  return m_overlays.first();
}

OverlayWindow* CaptureSession::overlayFor(const QPoint& canvasPos) const
{
  for (OverlayWindow* overlay : m_overlays) {
    if (overlay->canvasRect().contains(canvasPos))
      return overlay;
  }
  return nullptr;
}

void CaptureSession::updateActiveOverlay()
{
  if (!m_activeOverlay)
    return;
  const QPoint anchor = m_selection->hasSelection() ? m_selection->rect().center()
                                                    : m_activeOverlay->canvasRect().center();
  OverlayWindow* wanted = overlayFor(anchor);
  if (!wanted || wanted == m_activeOverlay)
    return;
  // The selection moved to another screen, so move the toolbars to that screen's overlay.
  m_activeOverlay = wanted;
  m_activeOverlay->attachToolbars(m_screenshotBar, m_editorBar, m_shareBar);
  m_activeOverlay->setFocus(Qt::OtherFocusReason);
}

void CaptureSession::refreshViews()
{
  for (OverlayWindow* overlay : m_overlays)
    overlay->update();
  if (m_activeOverlay)
    m_activeOverlay->positionToolbars();
}

qreal CaptureSession::selectionDpr() const
{
  const QPoint anchor = m_selection->hasSelection()
                            ? m_selection->rect().topLeft()
                            : (m_shots.isEmpty() ? QPoint() : m_shots.first().canvasRect.topLeft());
  for (const ScreenShot& shot : m_shots) {
    if (shot.canvasRect.contains(anchor))
      return qMax<qreal>(1.0, shot.dpr);
  }
  return m_shots.isEmpty() ? 1.0 : qMax<qreal>(1.0, m_shots.first().dpr);
}

QImage CaptureSession::composeResult() const
{
  if (!m_selection->hasSelection())
    return QImage();
  return composeResultImage(m_shots, m_selection->rect(), selectionDpr(),
                            m_drawState->objects());
}

void CaptureSession::commitPendingEdits()
{
  for (OverlayWindow* overlay : m_overlays)
    overlay->finishTextEdit();
}

void CaptureSession::onCopyRequested()
{
  // Without a selection there is nothing to copy; after a successful copy the UI closes whether
  // or not a balloon was shown.
  if (!m_selection->hasSelection())
    return;
  commitPendingEdits();
  const QImage image = composeResult();
  QString error;
  if (!copyImageToClipboard(image, &error)) {
    if (!error.isEmpty())
      emit balloon(balloonTitle(), error, QString());
    return;
  }
  if (Settings::instance().showBubbles())
    emit balloon(balloonTitle(), tr("Your screenshot is copied to clipboard"), QString());
  finish();
}

void CaptureSession::onSaveRequested(bool instant)
{
  if (!m_selection->hasSelection())
    return;
  commitPendingEdits();

  QString directory = saveDirectory();
  int format = Settings::instance().format();  // 1 PNG, 2 JPEG, 3 BMP
  QString path = uniqueScreenshotPath(directory, format);

  if (!instant) {
    // A normal save dialog with PNG/JPEG/BMP filters, default PNG; the chosen directory and
    // format are written back to the settings.
    QFileDialog dialog(m_activeOverlay ? static_cast<QWidget*>(m_activeOverlay) : nullptr,
                       tr("Save"), path);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({tr("PNG image (*.png)"), tr("JPEG image (*.jpg)"),
                           tr("BMP image (*.bmp)")});
    dialog.selectNameFilter(tr("PNG image (*.png)"));
    if (dialog.exec() != QDialog::Accepted)
      return;  // the capture UI stays open when the save dialog is cancelled
    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty())
      return;
    path = selected.first();
    // The filter order is fixed (PNG, JPEG, BMP), so the selection is matched by position and
    // stays correct when the filter texts are translated.
    const int filter = dialog.nameFilters().indexOf(dialog.selectedNameFilter());
    if (filter == 1)
      format = 2;
    else if (filter == 2)
      format = 3;
    else
      format = formatFromExtension(path);
    // The document portal grants exactly the name the dialog returned; a different name would
    // be written as a temporary file that never reaches the host folder.
    if (QFileInfo(path).suffix().isEmpty() && !isPortalExportPath(path))
      path += QLatin1Char('.') + saveExtension(format);
    Settings::instance().setLastSavedDir(savedDirectoryFor(path));
    Settings::instance().setValue(QStringLiteral("Format"), format);
  }

  const QImage image = composeResult();
  QString error;
  if (!saveImageToFile(image, path, format, Settings::instance().jpegQuality(), &error)) {
    emit balloon(balloonTitle(), error.isEmpty() ? QObject::tr("Cannot save the screenshot.") : error,
                 QString());
    return;
  }

  if (Settings::instance().showBubbles()) {
    const QString text = tr("Screenshot is saved to %1. Click here to open in the folder.")
                             .arg(QFileInfo(path).fileName());
    emit balloon(balloonTitle(), text, QFileInfo(path).absolutePath());
  }
  finish();
}

void CaptureSession::onPrintRequested()
{
  // The capture UI stays open after printing.
  if (!m_selection->hasSelection())
    return;
  commitPendingEdits();
  const QImage image = composeResult();
  QString error;
  if (!printImage(m_activeOverlay ? static_cast<QWidget*>(m_activeOverlay) : nullptr, image, &error)) {
    if (!error.isEmpty())
      emit balloon(balloonTitle(), error, QString());
    return;
  }
  refreshViews();
}

void CaptureSession::onUploadActionRequested(int shareMode)
{
  // Requires a selection; the capture UI closes before the upload starts, since the uploader
  // shows its own progress window. The upload service is opt-in, so nothing happens while the
  // user has not enabled it.
  if (!Settings::instance().uploadEnabled() || !m_selection->hasSelection())
    return;
  commitPendingEdits();
  const QImage image = composeResult();
  if (image.isNull())
    return;
  const qreal dpr = selectionDpr();
  finish();
  emit uploadRequested(image, shareMode, dpr);
}

void CaptureSession::onCloseRequested()
{
  finish();
}

void CaptureSession::onSelectionChanged()
{
  updateActiveOverlay();
  refreshViews();
}

void CaptureSession::finish()
{
  if (m_finished)
    return;
  m_finished = true;
  for (OverlayWindow* overlay : m_overlays)
    overlay->closeOverlay();
  emit finished();
}

} // namespace ls
