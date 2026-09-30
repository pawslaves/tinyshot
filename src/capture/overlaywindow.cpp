// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlaywindow.h"

#include "app/settings.h"
#include "toolbars.h"

#include <QAction>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace ls {

namespace {

// Cursor shape for the eight hit regions and the interior; a plain arrow whenever there is no
// selection or an annotation tool is active.
Qt::CursorShape cursorForRegion(HitRegion region)
{
  switch (region) {
  case HitRegion::Right:
  case HitRegion::Left:
    return Qt::SizeHorCursor;
  case HitRegion::Top:
  case HitRegion::Bottom:
    return Qt::SizeVerCursor;
  case HitRegion::TopLeft:
  case HitRegion::BottomRight:
    return Qt::SizeFDiagCursor;
  case HitRegion::TopRight:
  case HitRegion::BottomLeft:
    return Qt::SizeBDiagCursor;
  case HitRegion::Inside:
    return Qt::SizeAllCursor;
  case HitRegion::None:
    break;
  }
  return Qt::ArrowCursor;
}

// Everything outside the selection is drawn at half brightness.
const QColor kDimColor(0, 0, 0, 128);
const QColor kLabelBackground(0, 0, 0, 200);
const QColor kLabelText(0xDE, 0xDE, 0xDE);

constexpr int kTooltipOffsetY = 32;  // distance of the "Select area" hint below the pointer

// Rect with exclusive right/bottom edges; the toolbar and size-label placement works in these
// terms.
struct WRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  static WRect fromQt(const QRect& rect)
  {
    return WRect{rect.left(), rect.top(), rect.right() + 1, rect.bottom() + 1};
  }
  int width() const { return right - left; }
  int height() const { return bottom - top; }
  bool intersects(const WRect& other) const
  {
    return left < other.right && other.left < right && top < other.bottom && other.top < bottom;
  }
  QRect toQt() const { return QRect(left, top, width(), height()); }
};

} // namespace

OverlayWindow::OverlayWindow(const ScreenShot& shot, SelectionModel* selection, DrawState* drawState,
                             QWidget* parent)
    : QWidget(parent), m_shot(shot), m_selection(selection), m_drawState(drawState)
{
  // A frameless, always-on-top window covering one screen.
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::ArrowCursor);
  if (m_shot.screen)
    setScreen(m_shot.screen);
  setGeometry(m_shot.canvasRect);

  connect(m_selection, &SelectionModel::changed, this, &OverlayWindow::onSelectionChanged);
}

OverlayWindow::~OverlayWindow() = default;

QPoint OverlayWindow::toCanvas(const QPoint& local) const
{
  return local + m_shot.canvasRect.topLeft();
}

QPoint OverlayWindow::toLocal(const QPoint& canvas) const
{
  return canvas - m_shot.canvasRect.topLeft();
}

QRect OverlayWindow::toLocal(const QRect& canvas) const
{
  return canvas.translated(-m_shot.canvasRect.topLeft());
}

void OverlayWindow::showOverlay()
{
  showFullScreen();
}

void OverlayWindow::closeOverlay()
{
  cancelTextEdit();
  hide();
}

void OverlayWindow::onSelectionChanged()
{
  positionToolbars();
  update();
  emit selectionChanged();
}

// ---- toolbars ----

void OverlayWindow::attachToolbars(ScreenshotToolbar* shotBar, EditorToolbar* editBar,
                                   ShareToolbar* shareBar)
{
  m_screenshotBar = shotBar;
  m_editorBar = editBar;
  m_shareBar = shareBar;
  for (QWidget* widget : {static_cast<QWidget*>(shotBar), static_cast<QWidget*>(editBar),
                          static_cast<QWidget*>(shareBar)}) {
    if (!widget)
      continue;
    widget->setParent(this);
  }
  if (m_shareBar)
    m_shareBar->hide();
  positionToolbars();
}

void OverlayWindow::hideSharePopup()
{
  if (m_shareBar)
    m_shareBar->hide();
}

void OverlayWindow::showSharePopup()
{
  if (!m_shareBar || !m_screenshotBar || !m_screenshotBar->isVisible())
    return;
  // The share row opens under the toolbar, where the Share button sits, and flips above it
  // (using the flipped plate artwork) when it would leave the screen.
  const QSize size = m_shareBar->size();
  const QPoint barPos = m_screenshotBar->pos();
  int x = barPos.x();
  int y = barPos.y() + m_screenshotBar->height() + 4;
  bool flipped = false;
  if (y + size.height() > height()) {
    flipped = true;
    y = barPos.y() - size.height() - 4;
  }
  if (x + size.width() > width())
    x = std::max(0, width() - size.width());
  if (x < 0)
    x = 0;
  m_shareBar->setParent(this);
  m_shareBar->setFlipped(flipped);
  m_shareBar->move(x, y);
  m_shareBar->show();
  m_shareBar->raise();
}

void OverlayWindow::positionToolbars()
{
  if (!m_screenshotBar || !m_editorBar)
    return;
  // Visible as soon as a selection exists; hidden while a stroke is drawn, while the in-place
  // text editor is open and while a brand new selection is being dragged out.
  const bool visible = m_selection->hasSelection() && m_strokeTool == ToolId::None && !m_textEdit &&
                       !(m_selection->dragging() && m_selection->dragStartedNewSelection());
  if (!visible) {
    m_screenshotBar->hide();
    m_editorBar->hide();
    if (m_shareBar)
      m_shareBar->hide();
    return;
  }

  // Bars sit 5 px from the selection. Clamp against this screen rather than the whole desktop,
  // since each screen has its own overlay and a bar pushed onto the next screen would be clipped.
  const WRect sel = WRect::fromQt(m_selection->rect());
  const WRect canvas = WRect::fromQt(m_shot.canvasRect);
  const int shotW = m_screenshotBar->width();
  const int shotH = m_screenshotBar->height();
  const int editW = m_editorBar->width();
  const int editH = m_editorBar->height();

  // Screenshot toolbar: below the selection, right aligned.
  WRect rc1;
  rc1.left = sel.right - shotW;
  rc1.right = sel.right;
  rc1.top = sel.bottom + 5;
  rc1.bottom = rc1.top + shotH;
  if (rc1.left < canvas.left) {
    rc1.left = canvas.left;
    rc1.right = canvas.left + shotW;
  }
  if (rc1.bottom > canvas.bottom) {
    rc1.bottom = sel.top - 5;
    rc1.top = rc1.bottom - shotH;
    if (rc1.top < canvas.top) {
      rc1.bottom = sel.bottom - 5;
      rc1.top = rc1.bottom - shotH;
      rc1.left = sel.right - shotW - 5;
      rc1.right = sel.right - 5;
      if (rc1.left < canvas.left) {
        rc1.left = sel.right + 5;
        rc1.right = shotW + sel.right + 5;
      }
    }
  }

  // Edit toolbar: right of the selection, bottom aligned.
  WRect rc2;
  rc2.top = sel.bottom - editH;
  rc2.bottom = sel.bottom;
  if (rc2.top < canvas.top) {
    rc2.top = canvas.top;
    rc2.bottom = canvas.top + editH;
  }
  rc2.left = sel.right + 5;
  rc2.right = editW + sel.right + 5;
  if (rc2.right > canvas.right) {
    rc2.left = sel.left - editW - 5;
    rc2.right = sel.left - 5;
    if (rc2.left < canvas.left) {
      rc2.top = (sel.bottom - editH) - 5;
      rc2.left = sel.right - editW - 5;
      rc2.right = sel.right - 5;
      rc2.bottom = sel.bottom - 5;
      if (rc2.top < canvas.top) {
        rc2.top = canvas.top;
        rc2.bottom = canvas.top + editH;
      }
    }
  }

  // Keep the two bars apart.
  if (rc1.intersects(rc2)) {
    rc2.left = rc1.right + 5;
    rc2.right = editW + rc1.right + 5;
    if (rc2.right > canvas.right) {
      rc2.left = rc1.left - editW - 5;
      rc2.right = rc1.left - 5;
    }
    if (rc2.intersects(sel)) {
      rc2.top = rc1.top;
      rc2.bottom = editH + rc1.top;
      if (rc2.intersects(sel)) {
        rc2.top = rc1.bottom - editH;
        rc2.bottom = rc1.bottom;
      }
    }
  }

  m_screenshotBar->setParent(this);
  m_screenshotBar->move(toLocal(rc1.toQt().topLeft()));
  m_editorBar->setParent(this);
  m_editorBar->move(toLocal(rc2.toQt().topLeft()));
  m_screenshotBar->show();
  m_editorBar->show();
  m_screenshotBar->raise();
  m_editorBar->raise();
  if (m_shareBar && m_shareBar->isVisible())
    m_shareBar->raise();
}

// ---- painting ----

void OverlayWindow::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

  if (!m_shot.image.isNull()) {
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(size())), m_shot.image,
                      QRectF(QPointF(0, 0), QSizeF(m_shot.image.size())));
  } else {
    painter.fillRect(rect(), Qt::black);
  }

  drawDim(painter);
  drawAnnotationObjects(painter);
  if (m_selection->hasSelection()) {
    drawSelectionFrame(painter);
    drawSizeLabel(painter);
  }
  drawBrushPreview(painter);
}

void OverlayWindow::drawDim(QPainter& painter)
{
  if (!m_selection->hasSelection()) {
    painter.fillRect(rect(), kDimColor);
    return;
  }
  // Dim four rectangles around the selection, leaving the selection itself at full brightness.
  const QRect sel = toLocal(m_selection->rect()).intersected(rect());
  if (sel.isEmpty()) {
    painter.fillRect(rect(), kDimColor);
    return;
  }
  painter.fillRect(QRect(0, 0, width(), sel.top()), kDimColor);
  painter.fillRect(QRect(0, sel.bottom() + 1, width(), height() - sel.bottom() - 1), kDimColor);
  painter.fillRect(QRect(0, sel.top(), sel.left(), sel.height()), kDimColor);
  painter.fillRect(QRect(sel.right() + 1, sel.top(), width() - sel.right() - 1, sel.height()),
                   kDimColor);
}

void OverlayWindow::drawAnnotationObjects(QPainter& painter)
{
  if (m_drawState->objects().isEmpty() && !m_drawState->pending())
    return;
  painter.save();
  painter.setClipRect(rect());
  painter.translate(-m_shot.canvasRect.topLeft());
  for (const DrawObjectPtr& object : m_drawState->objects()) {
    if (object)
      object->draw(painter);
  }
  if (DrawObjectPtr pending = m_drawState->pending())
    pending->draw(painter);
  painter.restore();
}

void OverlayWindow::drawSelectionFrame(QPainter& painter)
{
  const QRect sel = toLocal(m_selection->rect());
  painter.save();
  painter.setPen(QPen(Qt::white, 1, Qt::DotLine));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRect(sel.topLeft(), QSize(std::max(1, sel.width() - 1),
                                             std::max(1, sel.height() - 1))));

  // Eight handles, 6x6 logical px unless the selection is smaller.
  painter.setPen(QPen(Qt::white, 1));
  painter.setBrush(Qt::black);
  const QVector<QRect> handles = m_selection->handleRects();
  for (const QRect& handle : handles) {
    const QRect local = toLocal(handle);
    painter.drawRect(QRect(local.topLeft(),
                           QSize(std::max(1, local.width() - 1), std::max(1, local.height() - 1))));
  }
  painter.restore();
}

void OverlayWindow::drawSizeLabel(QPainter& painter)
{
  // "WxH" in Arial 10pt bold, light grey on a translucent black box, 5 px above the selection.
  // If there's no room above it moves just inside the top edge, and it shifts left rather than
  // run off the right side of the screen.
  const WRect sel = WRect::fromQt(m_selection->rect());
  const WRect canvas = WRect::fromQt(m_shot.canvasRect);
  const QString text = QStringLiteral("%1x%2").arg(sel.width()).arg(sel.height());

  QFont font(QStringLiteral("Arial"), 10, QFont::Bold);
  painter.save();
  painter.setFont(font);
  const QFontMetrics metrics(font);
  const QRect textBounds = metrics.boundingRect(text);
  const int pad = 2;
  const QSize box(textBounds.width() + 2 * pad, textBounds.height() + 2 * pad);
  const int gap = 5;

  int x = sel.left;
  int y = sel.top - box.height() - gap;
  if (y < canvas.top) {
    x = sel.left + gap;
    y = sel.top + gap;
  }
  if (x + box.width() > canvas.right) {
    x = sel.left - gap - box.width();
    y = sel.top;
  }

  const QRect boxLocal = toLocal(QRect(x, y, box.width(), box.height()));
  painter.fillRect(boxLocal, kLabelBackground);
  painter.setPen(kLabelText);
  painter.drawText(boxLocal, Qt::AlignCenter, text);
  painter.restore();
}

void OverlayWindow::drawBrushPreview(QPainter& painter)
{
  // A brush-size circle follows the cursor while a tool is active and the pointer is inside the
  // window.
  if (m_drawState->tool() == ToolId::None || !m_mouseInside || m_textEdit)
    return;
  const int diameter = m_drawState->brushDiameter();
  if (diameter <= 0)
    return;
  const QPoint cursor = mapFromGlobal(QCursor::pos());
  if (!rect().contains(cursor))
    return;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(0, 0, 0), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(QPointF(cursor), diameter / 2.0, diameter / 2.0);
  painter.restore();
}

// ---- input ----

QPoint OverlayWindow::confineToScreen(const QPoint& canvasPos) const
{
  if (!ScreenGrabber::usePortal())
    return canvasPos;
  const QRect bounds = m_shot.canvasRect;
  return QPoint(std::clamp(canvasPos.x(), bounds.left(), bounds.right()),
                std::clamp(canvasPos.y(), bounds.top(), bounds.bottom()));
}

void OverlayWindow::updateCursorShape(const QPoint& canvasPos)
{
  if (m_drawState->tool() != ToolId::None || !m_selection->hasSelection()) {
    setCursor(Qt::ArrowCursor);
    return;
  }
  setCursor(cursorForRegion(m_selection->hitTest(canvasPos)));
}

void OverlayWindow::showSelectAreaTooltip(const QPoint& canvasPos)
{
  if (!m_tooltipActive)
    return;
  const QPoint global = mapToGlobal(toLocal(canvasPos)) + QPoint(0, kTooltipOffsetY);
  QToolTip::showText(global, tr("Select area"), this);
}

void OverlayWindow::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::RightButton) {
    event->ignore();  // handled by contextMenuEvent
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  // The first click removes the "Select area" tooltip.
  if (m_tooltipActive) {
    m_tooltipActive = false;
    QToolTip::hideText();
  }

  const QPoint canvas = confineToScreen(toCanvas(event->pos()));

  if (m_textEdit) {
    // A click outside the in-place editor commits it.
    if (!m_textEdit->geometry().contains(event->pos()))
      finishTextEdit();
    return;
  }

  // Hide the toolbars as soon as a stroke starts; they come back when the stroke is committed.
  if (m_drawState->tool() != ToolId::None) {
    if (m_screenshotBar)
      m_screenshotBar->hide();
    if (m_editorBar)
      m_editorBar->hide();
    hideSharePopup();
    beginStroke(canvas);
    return;
  }

  hideSharePopup();
  m_ctrlCopy = event->modifiers().testFlag(Qt::ControlModifier);
  m_selection->beginDrag(m_selection->hitTest(canvas), canvas);
  positionToolbars();
  update();
}

void OverlayWindow::beginStroke(const QPoint& canvasPos)
{
  m_strokeTool = m_drawState->tool();
  m_strokeAnchor = QPointF(canvasPos);
  const int diameter = m_drawState->brushDiameter();
  const QColor color = m_drawState->color();
  const QPointF p(canvasPos);

  DrawObjectPtr object;
  switch (m_strokeTool) {
  case ToolId::Pen:
  case ToolId::Marker:
    object = std::make_shared<StrokeObject>(m_strokeTool, QPolygonF() << p);
    break;
  case ToolId::Line:
  case ToolId::Arrow:
    object = std::make_shared<LineObject>(m_strokeTool, p, p);
    break;
  case ToolId::Rect:
    object = std::make_shared<RectObject>(QRectF(p, QSizeF(0, 0)));
    break;
  case ToolId::Text: {
    // In-place edit box; its geometry becomes the text box.
    finishTextEdit();
    m_textEdit = new QLineEdit(this);
    m_textEdit->setFont(TextObject::fontFor(m_drawState->brushDiameter(ToolId::Text,
                                                                      m_drawState->level())));
    const QFontMetrics metrics(m_textEdit->font());
    m_textEdit->setGeometry(QRect(toLocal(canvasPos),
                                  QSize(180, metrics.height() + 6)));
    m_textEdit->setFocus(Qt::MouseFocusReason);
    m_textEdit->show();
    connect(m_textEdit, &QLineEdit::returnPressed, this, &OverlayWindow::finishTextEdit);
    m_strokeTool = ToolId::None;
    positionToolbars();
    return;
  }
  case ToolId::None:
    m_strokeTool = ToolId::None;
    return;
  }

  if (object) {
    object->setStyle(diameter, color);
    m_drawState->setPending(object);
  }
  update();
}

void OverlayWindow::updateStroke(const QPoint& canvasPos)
{
  const DrawObjectPtr object = m_drawState->pending();
  if (!object)
    return;
  const QPointF p(canvasPos);
  switch (object->type()) {
  case ToolId::Pen:
  case ToolId::Marker:
    if (auto* stroke = dynamic_cast<StrokeObject*>(object.get()))
      stroke->appendPoint(p);
    break;
  case ToolId::Line:
  case ToolId::Arrow:
    if (auto* line = dynamic_cast<LineObject*>(object.get()))
      line->setEnd(p);
    break;
  case ToolId::Rect:
    if (auto* rect = dynamic_cast<RectObject*>(object.get())) {
      // The press point stays the anchor: normalizing against the current topLeft would shrink
      // the rectangle once the pointer crosses the anchor to the left or above it.
      rect->setRect(QRectF(m_strokeAnchor, p).normalized());
    }
    break;
  default:
    break;
  }
  update();
}

void OverlayWindow::commitStroke()
{
  if (m_strokeTool == ToolId::None) {
    m_drawState->discardPending();
  } else {
    // Undo removes completed objects only, so the pending stroke has to become one now.
    m_drawState->commitPending();
  }
  m_strokeTool = ToolId::None;
  positionToolbars();
  update();
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
  m_mouseInside = true;
  const QPoint canvas = confineToScreen(toCanvas(event->pos()));

  if (m_tooltipActive && !m_selection->hasSelection())
    showSelectAreaTooltip(canvas);

  if (m_strokeTool != ToolId::None) {
    updateStroke(canvas);
    setCursor(Qt::ArrowCursor);
    update();
    return;
  }

  if (m_selection->dragging()) {
    m_selection->dragTo(canvas);
    updateCursorShape(canvas);
    return;
  }

  updateCursorShape(canvas);
  update();
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }

  if (m_strokeTool != ToolId::None) {
    commitStroke();
    return;
  }

  if (m_selection->dragging()) {
    const bool ctrlCopy = m_ctrlCopy;
    m_ctrlCopy = false;
    m_selection->endDrag();
    positionToolbars();
    update();
    // The Ctrl flag captured on button-down makes the release copy the freshly selected area.
    if (ctrlCopy && m_selection->hasSelection())
      emit copyRequested();
    return;
  }

  positionToolbars();
  update();
}

void OverlayWindow::wheelEvent(QWheelEvent* event)
{
  // The wheel adjusts the active tool's brush/line width.
  const int delta = event->angleDelta().y();
  if (delta == 0)
    return;
  if (delta > 0)
    m_drawState->widthUp();
  else
    m_drawState->widthDown();
  update();
  event->accept();
}

void OverlayWindow::enterEvent(QEnterEvent* event)
{
  Q_UNUSED(event);
  m_mouseInside = true;
  update();
}

void OverlayWindow::leaveEvent(QEvent* event)
{
  Q_UNUSED(event);
  m_mouseInside = false;
  update();
}

// ---- shortcuts ----

void OverlayWindow::selectFullScreen()
{
  // Selects the whole virtual desktop and shows the toolbars.
  m_selection->setRect(m_selection->canvasBounds());
  positionToolbars();
  update();
}

void OverlayWindow::chooseColor()
{
  // Hides the toolbars, shows the colour dialog and stores the chosen colour on the active tool.
  const QColor initial = m_drawState->color();
  if (m_screenshotBar)
    m_screenshotBar->hide();
  if (m_editorBar)
    m_editorBar->hide();
  const QColor chosen = QColorDialog::getColor(initial, this, tr("Color"));
  if (chosen.isValid())
    m_drawState->setColor(chosen);
  positionToolbars();
  update();
}

void OverlayWindow::undo()
{
  // Removes the last completed object.
  m_drawState->undo();
  update();
}

void OverlayWindow::keyPressEvent(QKeyEvent* event)
{
  const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
  const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

  if (m_textEdit) {
    // The in-place editor owns the keyboard while it is open; Escape cancels it.
    if (event->key() == Qt::Key_Escape) {
      cancelTextEdit();
      event->accept();
      return;
    }
    QWidget::keyPressEvent(event);
    return;
  }

  if (ctrl) {
    switch (event->key()) {
    case Qt::Key_C:
      emit copyRequested();
      event->accept();
      return;
    case Qt::Key_S:
      emit saveRequested(true);  // true = save silently to the last-used folder, no Save As dialog
      event->accept();
      return;
    case Qt::Key_A:
      selectFullScreen();
      event->accept();
      return;
    case Qt::Key_X:
      emit closeRequested();
      event->accept();
      return;
    case Qt::Key_D:
      // The instant upload is part of the opt-in upload service; the key stays inert while it
      // is off.
      if (Settings::instance().uploadEnabled())
        emit uploadRequested(0);
      event->accept();
      return;
    case Qt::Key_P:
      emit printRequested();
      event->accept();
      return;
    case Qt::Key_Z:
      undo();
      event->accept();
      return;
    case Qt::Key_K:
      m_drawState->widthUp();
      update();
      event->accept();
      return;
    case Qt::Key_M:
      m_drawState->widthDown();
      update();
      event->accept();
      return;
    default:
      break;
    }
  }

  switch (event->key()) {
  case Qt::Key_Escape:
    // Cancel the current stroke and drop the tool; otherwise close the capture UI.
    if (m_drawState->pending()) {
      m_drawState->discardPending();
      m_strokeTool = ToolId::None;
      // Escape cancels the stroke and deactivates the tool, so the stroke is not restarted on
      // the next press.
      m_drawState->clearTool();
      positionToolbars();
      update();
    } else if (m_drawState->tool() != ToolId::None) {
      m_drawState->clearTool();
      update();
    } else {
      emit closeRequested();
    }
    event->accept();
    return;
  case Qt::Key_Left:
  case Qt::Key_Right:
  case Qt::Key_Up:
  case Qt::Key_Down: {
    // Arrows move the selection by 1 px, Shift+arrows resize its right/bottom edges by 1 px; the
    // selection model keeps it inside the virtual screen.
    int dx = 0;
    int dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:
      dx = -1;
      break;
    case Qt::Key_Right:
      dx = 1;
      break;
    case Qt::Key_Up:
      dy = -1;
      break;
    default:
      dy = 1;
      break;
    }
    if (shift)
      m_selection->resizeBy(dx, dy);
    else
      m_selection->moveBy(dx, dy);
    positionToolbars();
    update();
    event->accept();
    return;
  }
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

void OverlayWindow::contextMenuEvent(QContextMenuEvent* event)
{
  QMenu menu(this);
  // The upload service is opt-in: without it the menu starts at Print.
  QAction* upload = nullptr;
  if (Settings::instance().uploadEnabled())
    upload = menu.addAction(tr("Upload to prnt.sc") + QStringLiteral("\tCtrl+D"));
  QAction* print = menu.addAction(tr("Print") + QStringLiteral("\tCtrl+P"));
  QAction* copy = menu.addAction(tr("Copy") + QStringLiteral("\tCtrl+C"));
  QAction* save = menu.addAction(tr("Save") + QStringLiteral("\tCtrl+S"));
  menu.addSeparator();
  QAction* full = menu.addAction(tr("Select full screen"));
  QAction* clear = menu.addAction(tr("Clear selection"));
  QAction* cancel = menu.addAction(tr("Cancel"));

  QAction* chosen = menu.exec(event->globalPos());
  if (!chosen)
    return;
  if (chosen == upload)
    emit uploadRequested(0);
  else if (chosen == print)
    emit printRequested();
  else if (chosen == copy)
    emit copyRequested();
  else if (chosen == save)
    emit saveRequested(false);  // the Save menu entry opens the Save As dialog
  else if (chosen == full)
    selectFullScreen();
  else if (chosen == clear) {
    // Clear the selection and re-arm the "Select area" tooltip.
    m_selection->clear();
    m_tooltipActive = true;
    update();
  } else if (chosen == cancel) {
    emit closeRequested();
  }
  event->accept();
}

// ---- text editing ----

void OverlayWindow::finishTextEdit()
{
  if (!m_textEdit)
    return;
  QLineEdit* edit = m_textEdit;
  m_textEdit = nullptr;  // guard against re-entry through focus changes
  const QString text = edit->text();
  const QRect box(toCanvas(edit->geometry().topLeft()), edit->size());
  edit->hide();
  edit->deleteLater();

  if (!text.isEmpty()) {
    auto object = std::make_shared<TextObject>(QRectF(box), text);
    object->setStyle(m_drawState->brushDiameter(ToolId::Text, m_drawState->level()),
                     m_drawState->mainColor());
    m_drawState->addObject(object);
  }
  positionToolbars();
  update();
  setFocus(Qt::OtherFocusReason);
}

void OverlayWindow::cancelTextEdit()
{
  if (!m_textEdit)
    return;
  QLineEdit* edit = m_textEdit;
  m_textEdit = nullptr;
  edit->hide();
  edit->deleteLater();
  positionToolbars();
  update();
  setFocus(Qt::OtherFocusReason);
}

} // namespace ls
