// SPDX-License-Identifier: GPL-3.0-or-later

#include "drawstate.h"

#include <algorithm>

namespace ls {

const QColor DrawState::kDefaultColor = QColor(255, 0, 0);
const QColor DrawState::kDefaultMarkerColor = QColor(255, 255, 0);

DrawState::DrawState(QObject* parent) : QObject(parent) {}

void DrawState::setTool(ToolId t)
{
  if (m_tool == t)
    return;
  m_tool = t;
  emit styleChanged();
}

void DrawState::setLevel(int level)
{
  const int clamped = std::clamp(level, 0, kMaxLevel);
  if (clamped == m_level)
    return;
  m_level = clamped;
  if (m_pending)
    m_pending->setStyle(brushDiameter(), color());
  emit styleChanged();
}

int DrawState::brushDiameter(ToolId tool, int level)
{
  const int lv = std::clamp(level, 0, kMaxLevel);
  switch (tool) {
  case ToolId::Line:
  case ToolId::Pen:
  case ToolId::Arrow:
  case ToolId::Rect:
    return 3 + 2 * lv;
  case ToolId::Marker:
    return 16 + 2 * lv;
  case ToolId::Text:
    return 16 + 4 * lv;
  case ToolId::None:
    break;
  }
  return 0;
}

QColor DrawState::color() const
{
  return (m_tool == ToolId::Marker) ? m_markerColor : m_color;
}

void DrawState::setColor(const QColor& color)
{
  if (m_tool == ToolId::Marker)
    m_markerColor = color;
  else
    m_color = color;
  if (m_pending)
    m_pending->setStyle(brushDiameter(), this->color());
  emit styleChanged();
}

void DrawState::addObject(const DrawObjectPtr& object)
{
  if (!object)
    return;
  m_objects.append(object);
  emit objectsChanged();
}

void DrawState::setPending(const DrawObjectPtr& object)
{
  m_pending = object;
}

void DrawState::commitPending()
{
  if (!m_pending)
    return;
  DrawObjectPtr done = m_pending;
  m_pending.reset();
  m_objects.append(done);
  emit objectsChanged();
}

void DrawState::discardPending()
{
  if (!m_pending)
    return;
  m_pending.reset();
  emit objectsChanged();
}

void DrawState::undo()
{
  if (m_objects.isEmpty())
    return;
  m_objects.removeLast();
  emit objectsChanged();
}

} // namespace ls
