// SPDX-License-Identifier: GPL-3.0-or-later
//
// The full-screen capture window: one window per screen, all sharing a single SelectionModel.
#pragma once

#include "drawstate.h"
#include "screengrabber.h"
#include "selectionmodel.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>

class QLineEdit;

namespace ls {

class ScreenshotToolbar;
class EditorToolbar;
class ShareToolbar;

class OverlayWindow : public QWidget {
  Q_OBJECT

public:
  OverlayWindow(const ScreenShot& shot, SelectionModel* selection, DrawState* drawState,
                QWidget* parent = nullptr);
  ~OverlayWindow() override;

  QScreen* screen() const { return m_shot.screen; }
  QRect canvasRect() const { return m_shot.canvasRect; }

  void showOverlay();
  void closeOverlay();

  // Canvas coordinates span the logical virtual desktop; helpers convert to/from this window.
  QPoint toCanvas(const QPoint& local) const;
  QPoint toLocal(const QPoint& canvas) const;
  QRect toLocal(const QRect& canvas) const;

  // Toolbars are owned by the session and reparented to whichever overlay shows them.
  void attachToolbars(ScreenshotToolbar* shotBar, EditorToolbar* editBar, ShareToolbar* shareBar);
  void positionToolbars();
  void hideSharePopup();
  void showSharePopup();

  // The text tool edits in place through a floating QLineEdit.
  void finishTextEdit();
  void cancelTextEdit();

  // Opens the colour dialog for the session's toolbar button.
  void openColorPicker() { chooseColor(); }

signals:
  void copyRequested();
  void saveRequested(bool instant);      // toolbar Save shows the dialog, Ctrl+S does not
  void printRequested();
  void uploadRequested(int shareMode);
  void closeRequested();                 // Ctrl+X / Esc without a pending interaction
  void selectionChanged();

private slots:
  void onSelectionChanged();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  void drawDim(QPainter& painter);
  void drawAnnotationObjects(QPainter& painter);
  void drawSelectionFrame(QPainter& painter);
  void drawSizeLabel(QPainter& painter);
  void drawBrushPreview(QPainter& painter);
  void updateCursorShape(const QPoint& canvasPos);
  void selectFullScreen();
  void showSelectAreaTooltip(const QPoint& canvasPos);
  void beginStroke(const QPoint& canvasPos);
  void updateStroke(const QPoint& canvasPos);
  void commitStroke();
  void chooseColor();
  void undo();

  // On Wayland the portal gives no reliable multi-screen geometry, so a drag is confined to the
  // screen it started on; on X11 the canvas spans every screen.
  QPoint confineToScreen(const QPoint& canvasPos) const;

  ScreenShot m_shot;
  SelectionModel* m_selection = nullptr;
  DrawState* m_drawState = nullptr;

  ScreenshotToolbar* m_screenshotBar = nullptr;
  EditorToolbar* m_editorBar = nullptr;
  ShareToolbar* m_shareBar = nullptr;

  QLineEdit* m_textEdit = nullptr;

  bool m_ctrlCopy = false;        // Ctrl was held when the drag started
  bool m_tooltipActive = true;    // "Select area" tooltip, dropped on the first click
  bool m_mouseInside = false;
  ToolId m_strokeTool = ToolId::None;
  QPointF m_strokeAnchor;         // press point of the running stroke, canvas coordinates

  bool m_handlingSelection = false;
};

} // namespace ls
