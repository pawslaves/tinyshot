// SPDX-License-Identifier: GPL-3.0-or-later

#include "drawtools.h"

#include <QFontDatabase>

#include <cmath>

namespace ls {

QPen DrawObject::strokePen() const
{
  QPen pen(m_color);
  pen.setWidthF(qMax(1, m_width));
  if (m_type == ToolId::Marker) {
    // Marker: flat caps and a translucent colour, like a highlighter.
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    QColor c = m_color;
    c.setAlpha(125);
    pen.setColor(c);
  } else {
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
  }
  pen.setStyle(Qt::SolidLine);
  return pen;
}

void StrokeObject::drawMarkerDab(QPainter& painter, const QPointF& at, int diameter, const QColor& c)
{
  QColor fill = c;
  fill.setAlpha(125);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill);
  painter.drawEllipse(at, diameter / 2.0, diameter / 2.0);
  painter.restore();
}

void StrokeObject::draw(QPainter& painter) const
{
  if (m_points.isEmpty())
    return;
  if (m_type == ToolId::Marker) {
    if (m_points.size() == 1) {
      drawMarkerDab(painter, m_points.first(), m_width, m_color);
      return;
    }
    // A two-point marker stroke whose points coincide is a dab too.
    if (m_points.size() == 2 && m_points.at(0) == m_points.at(1)) {
      drawMarkerDab(painter, m_points.first(), m_width, m_color);
      return;
    }
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen = strokePen();
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPolyline(m_points);
  painter.restore();
}

void LineObject::draw(QPainter& painter) const
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(strokePen());
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(m_a, m_b);

  if (m_type == ToolId::Arrow) {
    // Filled triangle at the end point, scaled with the line width (at least 9 px long).
    const QLineF line(m_a, m_b);
    const qreal len = line.length();
    if (len > 1.0) {
      const qreal head = qMax<qreal>(9.0, 3.0 * m_width);
      const qreal width = qMax<qreal>(6.0, 2.0 * m_width);
      const qreal angle = std::atan2(-(m_b.y() - m_a.y()), m_b.x() - m_a.x());
      QPointF dir(std::cos(angle), -std::sin(angle));
      QPointF normal(-dir.y(), dir.x());
      const QPointF base = m_b - dir * head;
      QPolygonF head_poly;
      head_poly << m_b << (base + normal * (width / 2.0)) << (base - normal * (width / 2.0));
      painter.setPen(Qt::NoPen);
      painter.setBrush(m_color);
      painter.drawPolygon(head_poly);
    }
  }
  painter.restore();
}

void RectObject::draw(QPainter& painter) const
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(strokePen());
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(m_rect);
  painter.restore();
}

QFont TextObject::fontFor(int pixelSize)
{
  // Lightshot renders text in Calibri; use it when installed, otherwise the default font.
  QFont font;
  const QStringList families = QFontDatabase::families();
  if (families.contains(QStringLiteral("Calibri"), Qt::CaseInsensitive))
    font.setFamily(QStringLiteral("Calibri"));
  font.setPixelSize(qMax(1, pixelSize));
  return font;
}

void TextObject::draw(QPainter& painter) const
{
  if (m_text.isEmpty())
    return;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setPen(m_color);
  QFont font = fontFor(m_width);
  painter.setFont(font);
  painter.drawText(m_box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
  painter.restore();
}

} // namespace ls
