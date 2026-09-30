// SPDX-License-Identifier: GPL-3.0-or-later
//
// Annotation objects: one class per drawing tool, painted on top of the capture.
#pragma once

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <memory>

namespace ls {

enum class ToolId {
  None = 0,
  Line = 1,
  Pen = 2,     // freehand pencil
  Arrow = 3,
  Rect = 4,
  Marker = 5,  // translucent highlighter
  Text = 6,
};

class DrawObject {
public:
  explicit DrawObject(ToolId type) : m_type(type) {}
  virtual ~DrawObject() = default;

  DrawObject(const DrawObject&) = delete;
  DrawObject& operator=(const DrawObject&) = delete;

  ToolId type() const { return m_type; }
  virtual void draw(QPainter& painter) const = 0;

  void setStyle(int width, const QColor& color)
  {
    m_width = width;
    m_color = color;
  }

  // Marker strokes keep their flat caps, everything else uses round caps/joins.
  QPen strokePen() const;

protected:
  ToolId m_type;
  int m_width = 1;
  QColor m_color = QColor(Qt::red);
};

// Freehand polyline: the pen and the marker.
class StrokeObject : public DrawObject {
public:
  StrokeObject(ToolId type, const QPolygonF& points) : DrawObject(type), m_points(points) {}
  void draw(QPainter& painter) const override;
  void appendPoint(const QPointF& p) { m_points.append(p); }

  // A marker dab: a stroke of a single point becomes a filled circle of the brush diameter.
  static void drawMarkerDab(QPainter& painter, const QPointF& at, int diameter, const QColor& c);

private:
  QPolygonF m_points;
};

// Straight line; the arrow variant adds a head at the end point.
class LineObject : public DrawObject {
public:
  LineObject(ToolId type, const QPointF& a, const QPointF& b) : DrawObject(type), m_a(a), m_b(b) {}
  void draw(QPainter& painter) const override;
  void setEnd(const QPointF& b) { m_b = b; }

private:
  QPointF m_a;
  QPointF m_b;
};

class RectObject : public DrawObject {
public:
  RectObject(const QRectF& rect) : DrawObject(ToolId::Rect), m_rect(rect) {}
  void draw(QPainter& painter) const override;
  QRectF rect() const { return m_rect; }
  void setRect(const QRectF& rect) { m_rect = rect; }

private:
  QRectF m_rect;
};

// Text box.  The box and its text are plain data, painted on demand.  Font pixel size follows the
// text brush diameter.
class TextObject : public DrawObject {
public:
  TextObject(const QRectF& box, const QString& text) : DrawObject(ToolId::Text), m_box(box), m_text(text)
  {
  }
  void draw(QPainter& painter) const override;
  static QFont fontFor(int pixelSize);

private:
  QRectF m_box;
  QString m_text;
};

using DrawObjectPtr = std::shared_ptr<DrawObject>;

} // namespace ls
