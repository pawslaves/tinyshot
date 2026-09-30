// SPDX-License-Identifier: GPL-3.0-or-later

#include "selectionmodel.h"

#include <algorithm>

namespace ls {

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

QRect SelectionModel::clampIntoCanvas(const QRect& rect) const
{
  if (!m_bounds.isValid())
    return rect;
  // Keep the size (bounded by the canvas) and clamp the position, so a rectangle that starts
  // outside still lands fully inside instead of collapsing.
  const int width = std::min(rect.width(), m_bounds.width());
  const int height = std::min(rect.height(), m_bounds.height());
  const int x = std::clamp(rect.left(), m_bounds.left(), m_bounds.right() - width + 1);
  const int y = std::clamp(rect.top(), m_bounds.top(), m_bounds.bottom() - height + 1);
  return QRect(x, y, width, height);
}

void SelectionModel::setCanvasBounds(const QRect& bounds)
{
  m_bounds = bounds;
  if (m_hasSelection)
    setRect(m_rect);
}

void SelectionModel::setRect(const QRect& r)
{
  QRect rect = r.normalized();
  if (rect.width() < 1)
    rect.setWidth(1);
  if (rect.height() < 1)
    rect.setHeight(1);
  rect = clampIntoCanvas(rect);
  const bool wasValid = m_hasSelection;
  const QRect old = m_rect;
  m_rect = rect;
  m_hasSelection = true;
  if (!wasValid || old != rect)
    emit changed();
}

void SelectionModel::clear()
{
  if (!m_hasSelection)
    return;
  m_hasSelection = false;
  m_rect = QRect();
  m_dragMode = DragMode::None;
  emit changed();
}

QVector<QRect> SelectionModel::handleRects() const
{
  QVector<QRect> rects;
  rects.reserve(8);
  if (!m_hasSelection)
    return rects;

  // Edge handles sit at the edge midpoints; the values below are offsets from the left/top
  // edge - (right-left)/2 and (bottom-top)/2 - not centre coordinates.
  const int h = kHandleHalf;
  const int cx = (m_rect.right() - m_rect.left()) / 2;
  const int cy = (m_rect.bottom() - m_rect.top()) / 2;

  rects.append(QRect(m_rect.right() - h, m_rect.top() + cy - h, 2 * h, 2 * h));  // 0 right
  rects.append(QRect(m_rect.left() - h, m_rect.top() + cy - h, 2 * h, 2 * h));   // 1 left
  rects.append(QRect(m_rect.left() + cx - h, m_rect.top() - h, 2 * h, 2 * h));   // 2 top
  rects.append(QRect(m_rect.left() + cx - h, m_rect.bottom() - h, 2 * h, 2 * h));// 3 bottom
  rects.append(QRect(m_rect.left() - h, m_rect.top() - h, 2 * h, 2 * h));        // 4 TL
  rects.append(QRect(m_rect.right() - h, m_rect.top() - h, 2 * h, 2 * h));       // 5 TR
  rects.append(QRect(m_rect.left() - h, m_rect.bottom() - h, 2 * h, 2 * h));     // 6 BL
  rects.append(QRect(m_rect.right() - h, m_rect.bottom() - h, 2 * h, 2 * h));    // 7 BR
  return rects;
}

HitRegion SelectionModel::hitTest(const QPoint& canvasPos) const
{
  if (!m_hasSelection)
    return HitRegion::None;

  // The first handle rect that contains the point wins: edges (0-3) are tested before corners
  // (4-7), which only matters where they overlap on tiny selections; the interior is tested
  // last.
  static const HitRegion byIndex[8] = {
      HitRegion::Right,       HitRegion::Left,        HitRegion::Top,        HitRegion::Bottom,
      HitRegion::TopLeft,     HitRegion::TopRight,    HitRegion::BottomLeft, HitRegion::BottomRight};
  const QVector<QRect> rects = handleRects();
  for (int i = 0; i < rects.size(); ++i) {
    if (rects.at(i).contains(canvasPos))
      return byIndex[i];
  }
  if (m_rect.contains(canvasPos))
    return HitRegion::Inside;
  return HitRegion::None;
}

void SelectionModel::beginDrag(HitRegion region, const QPoint& canvasPos)
{
  m_dragOrigin = m_rect;
  m_dragStart = canvasPos;
  m_dragStartedNew = false;
  m_dragRegion = region;

  if (!m_hasSelection || region == HitRegion::None) {
    // No selection yet, or the click landed outside it: a brand new rubber-band selection
    // starts.
    m_dragMode = DragMode::New;
    m_dragStartedNew = true;
    m_hasSelection = false;
    m_rect = QRect();
    emit changed();
    return;
  }
  m_dragMode = (region == HitRegion::Inside) ? DragMode::Move : DragMode::Resize;
}

void SelectionModel::dragTo(const QPoint& canvasPos)
{
  switch (m_dragMode) {
  case DragMode::None:
    return;
  case DragMode::New:
    updateDragNewSelection(canvasPos);
    return;
  case DragMode::Move:
    updateDragMove(canvasPos);
    return;
  case DragMode::Resize:
    updateDragResize(canvasPos);
    return;
  }
}

void SelectionModel::updateDragNewSelection(const QPoint& pos)
{
  QRect r = QRect(m_dragStart, pos).normalized();
  if (r.width() < 1)
    r.setWidth(1);
  if (r.height() < 1)
    r.setHeight(1);
  if (m_bounds.isValid())
    r = r.intersected(m_bounds);
  const QRect old = m_rect;
  const bool wasValid = m_hasSelection;
  m_rect = r;
  m_hasSelection = r.width() > 0 && r.height() > 0;
  if (!wasValid || old != m_rect)
    emit changed();
}

void SelectionModel::updateDragResize(const QPoint& pos)
{
  QRect r = m_dragOrigin;
  switch (m_dragRegion) {
  case HitRegion::Right:
  case HitRegion::TopRight:
  case HitRegion::BottomRight:
    r.setRight(std::max(pos.x(), r.left() + 1));
    break;
  case HitRegion::Left:
  case HitRegion::TopLeft:
  case HitRegion::BottomLeft:
    r.setLeft(std::min(pos.x(), r.right() - 1));
    break;
  default:
    break;
  }
  switch (m_dragRegion) {
  case HitRegion::Bottom:
  case HitRegion::BottomLeft:
  case HitRegion::BottomRight:
    r.setBottom(std::max(pos.y(), r.top() + 1));
    break;
  case HitRegion::Top:
  case HitRegion::TopLeft:
  case HitRegion::TopRight:
    r.setTop(std::min(pos.y(), r.bottom() - 1));
    break;
  default:
    break;
  }
  // Resize live; the result is clamped inside the virtual screen.
  if (m_bounds.isValid())
    r = r.intersected(m_bounds);
  if (r.width() < 1 || r.height() < 1)
    return;
  const QRect old = m_rect;
  m_rect = r;
  if (old != m_rect)
    emit changed();
}

void SelectionModel::updateDragMove(const QPoint& pos)
{
  QRect r = m_dragOrigin.translated(pos - m_dragStart);
  if (m_bounds.isValid()) {
    // Keep the size and clamp the position: the rectangle follows the cursor until it reaches
    // the screen edge and then stops.
    if (r.left() < m_bounds.left())
      r.moveLeft(m_bounds.left());
    if (r.top() < m_bounds.top())
      r.moveTop(m_bounds.top());
    if (r.right() > m_bounds.right())
      r.moveRight(m_bounds.right());
    if (r.bottom() > m_bounds.bottom())
      r.moveBottom(m_bounds.bottom());
  }
  const QRect old = m_rect;
  m_rect = r;
  m_hasSelection = true;
  if (old != m_rect)
    emit changed();
}

void SelectionModel::endDrag()
{
  if (m_dragMode == DragMode::None)
    return;
  m_dragMode = DragMode::None;
  if (m_hasSelection && (m_rect.width() < 1 || m_rect.height() < 1))
    clear();
}

void SelectionModel::moveBy(int dx, int dy)
{
  if (!m_hasSelection)
    return;
  const QRect r = m_rect.translated(dx, dy);
  if (m_bounds.isValid() && !m_bounds.contains(r))
    return;  // a clipped move is dropped, not slid
  m_rect = r;
  emit changed();
}

void SelectionModel::resizeBy(int dx, int dy)
{
  if (!m_hasSelection)
    return;
  QRect r = m_rect;
  r.setRight(r.right() + dx);
  r.setBottom(r.bottom() + dy);
  if (r.width() < 1 || r.height() < 1)
    return;
  if (m_bounds.isValid() && !m_bounds.contains(r))
    return;  // a resize the screen clamp would change is dropped
  m_rect = r;
  emit changed();
}

} // namespace ls
