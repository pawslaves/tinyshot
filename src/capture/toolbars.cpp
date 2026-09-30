// SPDX-License-Identifier: GPL-3.0-or-later

#include "toolbars.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

namespace ls {

namespace {

// The plate is a fixed ink tone (DESIGN.md): it sits on top of an arbitrary screenshot, so it
// does not follow the palette.  Flat fills, no gradients or shadows.
const QColor kPlateFill(0x20, 0x1d, 0x1d);
const QColor kPlateBorder(0x30, 0x2c, 0x2c);
const QColor kSeparator(0x30, 0x2c, 0x2c);
const QColor kHoverFill(0x30, 0x2c, 0x2c);
const QColor kPressedFill(0x0f, 0x00, 0x00);
const QColor kCheckedFill(0x00, 0x7a, 0xff);

constexpr int kPlateRadius = 4;
constexpr int kButtonRadius = 3;
constexpr qreal kDisabledIconOpacity = 0.4;
constexpr int kTailWidth = 12;
constexpr int kTailHeight = 4;

// The share popup lines up with the left edge of the screenshot bar, so the notch sits under the
// centre of its Share button (x 35..59 of the bar).
constexpr int kShareTailX = 47;

QString iconPath(const QString& name)
{
  return QStringLiteral(":/toolbar/") + name + QStringLiteral(".svg");
}

} // namespace

ArtButton::ArtButton(const QString& artName, const QSize& logicalSize, qreal devicePixelRatio,
                     QWidget* parent)
  : QWidget(parent)
  , m_icon(artName.isEmpty() ? QIcon() : QIcon(iconPath(artName)))
  , m_dpr(devicePixelRatio)
{
  setFixedSize(logicalSize);
  setCursor(Qt::ArrowCursor);
}

void ArtButton::setChecked(bool checked)
{
  if (m_checked == checked)
    return;
  m_checked = checked;
  update();
}

QRect ArtButton::highlightRect() const
{
  return rect().adjusted(0, 0, -1, -1);
}

QRect ArtButton::glyphRect() const
{
  // The glyphs have their own padding inside the 24x24 grid, so a slightly larger square keeps
  // the drawn line weight right.
  const int side = qMin(width(), height()) + 2;
  return QRect(QPoint((width() - side) / 2, (height() - side) / 2), QSize(side, side));
}

void ArtButton::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  if (m_checked || m_hover || m_pressed) {
    QColor fill = kHoverFill;
    if (m_checked)
      fill = kCheckedFill;
    else if (m_pressed)
      fill = kPressedFill;
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(highlightRect(), kButtonRadius, kButtonRadius);
  }

  paintExtra(painter);

  if (!m_icon.isNull()) {
    const QRect target = glyphRect();
    if (!isEnabled())
      painter.setOpacity(kDisabledIconOpacity);
    painter.drawPixmap(target.topLeft(), m_icon.pixmap(target.size(), m_dpr));
  }
}

void ArtButton::enterEvent(QEnterEvent* event)
{
  Q_UNUSED(event);
  m_hover = true;
  update();
}

void ArtButton::leaveEvent(QEvent* event)
{
  Q_UNUSED(event);
  m_hover = false;
  m_pressed = false;
  update();
}

void ArtButton::mousePressEvent(QMouseEvent* event)
{
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  m_pressed = true;
  update();
  event->accept();
}

void ArtButton::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  const bool wasPressed = m_pressed;
  m_pressed = false;
  update();
  if (wasPressed && rect().contains(event->pos())) {
    if (m_checkable)
      setChecked(!m_checked);
    emit clicked();
  }
  event->accept();
}

ColorButton::ColorButton(const QSize& logicalSize, qreal devicePixelRatio, QWidget* parent)
  : ArtButton(QString(), logicalSize, devicePixelRatio, parent)
{
}

void ColorButton::setColor(const QColor& color)
{
  if (m_color == color)
    return;
  m_color = color;
  update();
}

void ColorButton::paintExtra(QPainter& painter)
{
  // The outline keeps pale colours (white, yellow) visible on the dark plate.
  const QRectF swatch = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
  painter.setPen(QPen(QColor(0xfd, 0xfc, 0xfc, 160), 1.4));
  painter.setBrush(m_color);
  painter.drawRoundedRect(swatch, 3, 3);
}

ToolbarWidget::ToolbarWidget(const QSize& logicalSize, qreal devicePixelRatio, QWidget* parent)
  : QWidget(parent), m_dpr(devicePixelRatio)
{
  setFixedSize(logicalSize);
}

ArtButton* ToolbarWidget::addButton(const QString& artName, const QSize& logicalSize,
                                    const QPoint& pos, const QString& tooltip)
{
  auto* button = new ArtButton(artName, logicalSize, m_dpr, this);
  button->move(pos);
  button->setToolTip(tooltip);
  return button;
}

void ToolbarWidget::setTail(int centerX, bool onTop)
{
  m_tailX = centerX;
  m_tailOnTop = onTop;
  update();
}

void ToolbarWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const int tail = m_tailX >= 0 ? kTailHeight : 0;
  const QRectF plate = m_tailOnTop ? QRectF(rect().adjusted(0, tail, -1, -1))
                                   : QRectF(rect().adjusted(0, 0, -1, -1 - tail));

  painter.setPen(Qt::NoPen);
  painter.setBrush(kPlateFill);
  painter.drawRoundedRect(plate, kPlateRadius, kPlateRadius);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(kPlateBorder, 1.0));
  painter.drawRoundedRect(plate, kPlateRadius, kPlateRadius);

  if (m_tailX >= 0) {
    const qreal base = m_tailOnTop ? plate.top() : plate.bottom();
    const qreal apex = m_tailOnTop ? 0.5 : height() - 0.5;
    QPolygonF notch;
    notch << QPointF(m_tailX, apex) << QPointF(m_tailX + kTailWidth / 2.0, base)
          << QPointF(m_tailX - kTailWidth / 2.0, base);
    // Fill over the plate border so the notch opens into the plate, then outline its flanks.
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPlateFill);
    painter.drawPolygon(notch);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kPlateBorder, 1.0));
    painter.drawLine(notch.at(0), notch.at(1));
    painter.drawLine(notch.at(0), notch.at(2));
  }

  if (m_separatorX >= 0) {
    painter.setPen(QPen(kSeparator, 1.0));
    painter.drawLine(QPointF(m_separatorX + 0.5, plate.top() + 6),
                     QPointF(m_separatorX + 0.5, plate.bottom() - 6));
  }
}

ScreenshotToolbar::ScreenshotToolbar(qreal devicePixelRatio, QWidget* parent)
  : ToolbarWidget(QSize(204, 29), devicePixelRatio, parent)
{
  const QSize buttonSize(24, 20);
  const int y = 4;

  const auto add = [&](const QString& name, const QString& tooltip,
                       void (ScreenshotToolbar::*signal)()) {
    ArtButton* button = addButton(name, buttonSize, QPoint(0, y), tooltip);
    connect(button, &ArtButton::clicked, this, signal);
    return button;
  };

  m_uploadButton = add(QStringLiteral("upload"), tr("Upload to prnt.sc (Ctrl+D)"),
                       &ScreenshotToolbar::uploadRequested);
  m_shareButton = add(QStringLiteral("share"), tr("Share on social networks"),
                      &ScreenshotToolbar::shareRequested);
  m_googleButton = add(QStringLiteral("google"), tr("Search similar images on Google (Ctrl+G)"),
                       &ScreenshotToolbar::googleRequested);
  m_printButton = add(QStringLiteral("print"), tr("Print (Ctrl+P)"),
                      &ScreenshotToolbar::printRequested);
  m_copyButton = add(QStringLiteral("copy"), tr("Copy (Ctrl+C)"),
                     &ScreenshotToolbar::copyRequested);
  m_saveButton = add(QStringLiteral("save"), tr("Save (Ctrl+S)"),
                     &ScreenshotToolbar::saveRequested);
  m_cancelButton = add(QStringLiteral("cancel"), tr("Close (Ctrl+X)"),
                       &ScreenshotToolbar::cancelRequested);

  applyLayout();
}

void ScreenshotToolbar::setUploadEnabled(bool enabled)
{
  if (m_uploadEnabled == enabled)
    return;
  m_uploadEnabled = enabled;
  for (ArtButton* button : {m_uploadButton, m_shareButton, m_googleButton})
    button->setVisible(enabled);
  applyLayout();
}

void ScreenshotToolbar::applyLayout()
{
  // Toolbar geometry: 24x20 buttons at y 4, the plate 4 px wider than the last button, and a
  // separator before Close.  With uploads off the leading buttons go away and the rest slides
  // left, keeping the same gaps.
  constexpr int kButton = 24;
  const int y = 4;
  int x = 5;
  const auto place = [&](ArtButton* button, int gapAfter) {
    button->move(x, y);
    x += kButton + gapAfter;
  };

  if (m_uploadEnabled) {
    place(m_uploadButton, 6);
    place(m_shareButton, 2);
    place(m_googleButton, 3);
  }
  place(m_printButton, 4);
  place(m_copyButton, 5);
  place(m_saveButton, 7);
  m_cancelButton->move(x, y);

  setSeparatorX(x - 4);
  setFixedSize(x + kButton + 4, 29);
}

EditorToolbar::EditorToolbar(qreal devicePixelRatio, QWidget* parent)
  : ToolbarWidget(QSize(29, 204), devicePixelRatio, parent)
{
  const QSize buttonSize(20, 20);
  const int x = 4;
  int y = 9;
  const auto addTool = [&](const QString& name, ToolId tool, const QString& tooltip) {
    ArtButton* button = addButton(name, buttonSize, QPoint(x, y), tooltip);
    button->setCheckable(true);
    connect(button, &ArtButton::clicked, this, [this, tool]() { emit toolSelected(tool); });
    m_toolButtons.insert(static_cast<int>(tool), button);
    y += buttonSize.height() + 4;
    return button;
  };

  addTool(QStringLiteral("pen"), ToolId::Pen, tr("Pen"));
  addTool(QStringLiteral("line"), ToolId::Line, tr("Line"));
  addTool(QStringLiteral("arrow"), ToolId::Arrow, tr("Arrow"));
  addTool(QStringLiteral("rect"), ToolId::Rect, tr("Rectangle"));
  addTool(QStringLiteral("marker"), ToolId::Marker, tr("Marker"));
  addTool(QStringLiteral("text"), ToolId::Text, tr("Text"));

  // The colour button has no check state; it opens the colour picker.
  m_colorButton = new ColorButton(buttonSize, dpr(), this);
  m_colorButton->move(x, y);
  m_colorButton->setToolTip(tr("Color"));
  connect(m_colorButton, &ArtButton::clicked, this, &EditorToolbar::colorRequested);
  y += buttonSize.height() + 6;  // Undo sits 6 px below the colour button, not 4 px like the tools

  m_undoButton = addButton(QStringLiteral("undo"), buttonSize, QPoint(x, y), tr("Undo (Ctrl+Z)"));
  connect(m_undoButton, &ArtButton::clicked, this, &EditorToolbar::undoRequested);
}

void EditorToolbar::setActiveTool(ToolId tool)
{
  for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
    it.value()->setChecked(it.key() == static_cast<int>(tool));
}

void EditorToolbar::setColorSwatch(const QColor& color)
{
  if (m_colorButton)
    m_colorButton->setColor(color);
}

ShareToolbar::ShareToolbar(qreal devicePixelRatio, QWidget* parent)
  : ToolbarWidget(QSize(84, 24), devicePixelRatio, parent)
{
  const QSize buttonSize(16, 16);
  int x = 4;
  const auto add = [&](const QString& name, int shareMode, const QString& tooltip) {
    ArtButton* button = addButton(name, buttonSize, QPoint(x, 0), tooltip);
    connect(button, &ArtButton::clicked, this,
            [this, shareMode]() { emit shareRequested(shareMode); });
    x += buttonSize.width() + 4;
    m_buttons.append(button);
    return button;
  };

  add(QStringLiteral("twitter"), 1, tr("Share on Twitter"));
  add(QStringLiteral("facebook"), 2, tr("Share on Facebook"));
  add(QStringLiteral("vk"), 4, tr("Share on VK"));
  add(QStringLiteral("pinterest"), 5, tr("Share on Pinterest"));

  setFlipped(false);
}

void ShareToolbar::setFlipped(bool flipped)
{
  // Not flipped the popup hangs below the screenshot bar, so the plate keeps a strip at the top
  // for the notch and the buttons sit under it; flipped it is the other way round.
  setTail(kShareTailX, !flipped);
  const int y = flipped ? 2 : 6;
  for (ArtButton* button : m_buttons)
    button->move(button->x(), y);
}

} // namespace ls
