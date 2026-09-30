// SPDX-License-Identifier: GPL-3.0-or-later
//
// Annotation state: the active tool, the brush width level, the two colour slots and the list
// of drawn objects with its undo stack.
#pragma once

#include "drawtools.h"

#include <QColor>
#include <QObject>
#include <QVector>

namespace ls {

class DrawState : public QObject {
  Q_OBJECT

public:
  explicit DrawState(QObject* parent = nullptr);

  static constexpr int kMaxLevel = 10;
  static const QColor kDefaultColor;
  static const QColor kDefaultMarkerColor;

  ToolId tool() const { return m_tool; }
  void setTool(ToolId t);
  void clearTool() { setTool(ToolId::None); }

  int level() const { return m_level; }
  void setLevel(int level);                 // clamped to 0..kMaxLevel
  void widthUp() { setLevel(m_level + 1); }
  void widthDown() { setLevel(m_level - 1); }

  // Brush diameter in logical pixels; the sizes follow Lightshot's brushes.
  static int brushDiameter(ToolId tool, int level);
  int brushDiameter() const { return brushDiameter(m_tool, m_level); }

  // Each tool kind has its own colour slot: the marker keeps a separate one, everything else
  // shares the main colour.
  QColor color() const;
  void setColor(const QColor& color);
  QColor mainColor() const { return m_color; }

  // The pending object is the stroke being drawn right now; it joins the object list only when
  // it is committed.
  void addObject(const DrawObjectPtr& object);
  void undo();
  const QVector<DrawObjectPtr>& objects() const { return m_objects; }

  DrawObjectPtr pending() const { return m_pending; }
  void setPending(const DrawObjectPtr& object);
  void commitPending();
  void discardPending();

signals:
  void styleChanged();     // emitted when the width level or colour changes (brush preview follows)
  void objectsChanged();   // emitted when the object list changes (undo, commit, discard)

private:
  ToolId m_tool = ToolId::None;
  int m_level = 0;
  QColor m_color = kDefaultColor;
  QColor m_markerColor = kDefaultMarkerColor;
  QVector<DrawObjectPtr> m_objects;
  DrawObjectPtr m_pending;
};

} // namespace ls
