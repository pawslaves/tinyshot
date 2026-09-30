// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three capture toolbars.  The plate, the button states and the colour swatch are painted
// in code; the glyphs are SVG files compiled into the qrc under :/toolbar/.
#pragma once

#include "drawtools.h"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class QEnterEvent;

namespace ls {

// One button on a plate.  The glyph is rendered at the widget's device pixel ratio, so it stays
// crisp on HiDPI screens.
class ArtButton : public QWidget {
  Q_OBJECT

public:
  ArtButton(const QString& artName, const QSize& logicalSize, qreal devicePixelRatio,
            QWidget* parent = nullptr);

  void setChecked(bool checked);
  bool isChecked() const { return m_checked; }
  void setCheckable(bool checkable) { m_checkable = checkable; }

signals:
  void clicked();

protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

  // Painted over the state highlight and below the glyph.
  virtual void paintExtra(QPainter& painter) { Q_UNUSED(painter); }

private:
  QRect highlightRect() const;
  QRect glyphRect() const;

  QIcon m_icon;
  qreal m_dpr = 1.0;
  bool m_hover = false;
  bool m_pressed = false;
  bool m_checked = false;
  bool m_checkable = false;
};

// Colour button: a swatch of the active tool colour with a light outline, drawn in code.
class ColorButton : public ArtButton {
  Q_OBJECT

public:
  ColorButton(const QSize& logicalSize, qreal devicePixelRatio, QWidget* parent = nullptr);
  void setColor(const QColor& color);

protected:
  void paintExtra(QPainter& painter) override;

private:
  QColor m_color = Qt::red;
};

// A plate window with child buttons; the plate is a rounded dark translucent rectangle painted
// in code.  setTail() adds the little notch the share popup points at the screenshot bar with.
class ToolbarWidget : public QWidget {
  Q_OBJECT

public:
  ToolbarWidget(const QSize& logicalSize, qreal devicePixelRatio, QWidget* parent = nullptr);

protected:
  ArtButton* addButton(const QString& artName, const QSize& logicalSize, const QPoint& pos,
                       const QString& tooltip);
  void setTail(int centerX, bool onTop);
  void setSeparatorX(int x)
  {
    m_separatorX = x;
    update();
  }
  qreal dpr() const { return m_dpr; }
  void paintEvent(QPaintEvent* event) override;

private:
  qreal m_dpr = 1.0;
  int m_tailX = -1;
  bool m_tailOnTop = true;
  int m_separatorX = -1;
};

// Horizontal capture toolbar: Upload, Share, Google, Print, Copy, Save, Close.
class ScreenshotToolbar : public ToolbarWidget {
  Q_OBJECT

public:
  explicit ScreenshotToolbar(qreal devicePixelRatio, QWidget* parent = nullptr);

  // With uploads off the Upload, Share and Google buttons are hidden and the bar shrinks to the
  // remaining four; OverlayWindow places the bar by its size, so nothing else has to change.
  void setUploadEnabled(bool enabled);

signals:
  void uploadRequested();
  void shareRequested();
  void googleRequested();
  void printRequested();
  void copyRequested();
  void saveRequested();
  void cancelRequested();

private:
  void applyLayout();

  ArtButton* m_uploadButton = nullptr;
  ArtButton* m_shareButton = nullptr;
  ArtButton* m_googleButton = nullptr;
  ArtButton* m_printButton = nullptr;
  ArtButton* m_copyButton = nullptr;
  ArtButton* m_saveButton = nullptr;
  ArtButton* m_cancelButton = nullptr;
  bool m_uploadEnabled = true;
};

// Vertical editor toolbar: the annotation tools, the colour swatch and Undo.
class EditorToolbar : public ToolbarWidget {
  Q_OBJECT

public:
  explicit EditorToolbar(qreal devicePixelRatio, QWidget* parent = nullptr);

  void setActiveTool(ToolId tool);
  void setColorSwatch(const QColor& color);

signals:
  void toolSelected(ls::ToolId tool);
  void colorRequested();
  void undoRequested();

private:
  QHash<int, ArtButton*> m_toolButtons;
  ArtButton* m_undoButton = nullptr;
  ColorButton* m_colorButton = nullptr;
};

// Popup share toolbar: Twitter, Facebook, VK, Pinterest.
class ShareToolbar : public ToolbarWidget {
  Q_OBJECT

public:
  explicit ShareToolbar(qreal devicePixelRatio, QWidget* parent = nullptr);

  // The popup opens below the screenshot bar, or above it when there is no room below.  The
  // notch then points up or down and the buttons stay centred on the plate.
  void setFlipped(bool flipped);

signals:
  void shareRequested(int shareMode);  // an ls::ShareMode value

private:
  QList<ArtButton*> m_buttons;
};

} // namespace ls
