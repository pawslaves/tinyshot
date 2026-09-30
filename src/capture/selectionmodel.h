// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selection geometry of the capture UI.  Coordinates are canvas coordinates: the logical virtual
// desktop, i.e. the union of all QScreen::geometry() rectangles.  The layout constants are the
// 96-dpi values used by the UI and apply unchanged at every device pixel ratio.
#pragma once

#include <QObject>
#include <QRect>
#include <QVector>

namespace ls {

// Nine hit regions.  The order of 0..7 matches handleRects():
// 0 = right edge, 1 = left edge, 2 = top edge, 3 = bottom edge, 4 = top-left corner,
// 5 = top-right corner, 6 = bottom-left corner, 7 = bottom-right corner, 8 = interior (move).
enum class HitRegion {
  None = -1,
  Right = 0,
  Left = 1,
  Top = 2,
  Bottom = 3,
  TopLeft = 4,
  TopRight = 5,
  BottomLeft = 6,
  BottomRight = 7,
  Inside = 8,
};

class SelectionModel : public QObject {
  Q_OBJECT

public:
  explicit SelectionModel(QObject* parent = nullptr);

  // Whole virtual desktop in canvas coordinates; all clamping happens against it.
  void setCanvasBounds(const QRect& bounds);
  QRect canvasBounds() const { return m_bounds; }

  bool hasSelection() const { return m_hasSelection; }
  QRect rect() const { return m_rect; }          // valid only when hasSelection()
  void setRect(const QRect& r);                  // clamped to the canvas, marks the selection valid
  void clear();

  // Handle half size in logical px; a handle is 6x6 logical px unless the selection is smaller.
  static constexpr int kHandleHalf = 3;

  // Eight handle rectangles (indices match HitRegion 0..7) in canvas coordinates.
  QVector<QRect> handleRects() const;

  // Hit test against the nine regions; HitRegion::None when there is no selection.
  HitRegion hitTest(const QPoint& canvasPos) const;

  // Mouse drag: beginDrag() records what is being dragged and dragTo() applies the live update
  // (clamped into the canvas, minimum size 1x1).
  void beginDrag(HitRegion region, const QPoint& canvasPos);
  void dragTo(const QPoint& canvasPos);
  void endDrag();
  bool dragging() const { return m_dragMode != DragMode::None; }
  bool dragStartedNewSelection() const { return m_dragStartedNew; }

  // Keyboard: arrows move by 1 px, Shift+arrows resize the right/bottom edges by 1 px.  Both
  // drop a change that would leave the virtual screen, so a clipped move stops instead of
  // sliding.
  void moveBy(int dx, int dy);
  void resizeBy(int dx, int dy);

signals:
  // Emitted whenever the selection rectangle or its validity changed.
  void changed();

private:
  enum class DragMode { None, New, Resize, Move };

  QRect clampIntoCanvas(const QRect& rect) const;
  void updateDragNewSelection(const QPoint& pos);
  void updateDragResize(const QPoint& pos);
  void updateDragMove(const QPoint& pos);

  QRect m_bounds;
  QRect m_rect;
  bool m_hasSelection = false;

  DragMode m_dragMode = DragMode::None;
  HitRegion m_dragRegion = HitRegion::None;
  QRect m_dragOrigin;             // selection when the drag began
  QPoint m_dragStart;             // mouse position when the drag began
  bool m_dragStartedNew = false;  // the drag creates a new rubber-band selection
};

} // namespace ls
