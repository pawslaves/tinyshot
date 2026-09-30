// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/multipart.h"

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <cstdlib>

namespace ls {
namespace {

// The 27-dash boundary prefix Lightshot uses.
constexpr char kBoundaryPrefix[] = "---------------------------";

// Field values go out as UTF-8, which the endpoint accepts.
QByteArray toWire(const QString& text)
{
  return text.toUtf8();
}

// Lightshot fills the boundary with MSVC rand() values, which never exceed 0x7FFF, so each
// hex group is exactly four digits. Stay in that range to keep the boundary the same length.
quint32 randValue()
{
  return QRandomGenerator::global()->bounded(0x8000u);
}

QByteArray hex4(quint32 value)
{
  return QByteArray::number(value, 16).rightJustified(4, '0').toUpper();
}

} // namespace

QByteArray makeBoundary(quint32 r1, quint32 r2, quint32 r3)
{
  QByteArray boundary(kBoundaryPrefix);
  boundary += hex4(r1);
  boundary += hex4(r2);
  boundary += hex4(r3);
  return boundary;
}

QByteArray randomBoundary()
{
  return makeBoundary(randValue(), randValue(), randValue());
}

std::optional<QByteArray> buildMultipartBody(const QByteArray& boundary,
                                             const QList<MultipartTextPart>& textParts,
                                             const QList<MultipartFilePart>& fileParts)
{
  QByteArray body;
  // Rough sizing so the common case does not reallocate while the image bytes are appended.
  body.reserve(512 + 64 * (textParts.size() + fileParts.size()));

  for (const MultipartTextPart& part : textParts) {
    body += "--";
    body += boundary;
    body += "\r\nContent-Disposition: form-data; name=\"";
    body += toWire(part.name);
    body += "\"\r\n\r\n";
    body += toWire(part.value);
    body += "\r\n";
  }

  for (const MultipartFilePart& part : fileParts) {
    QFile file(part.path);
    if (!file.open(QIODevice::ReadOnly))
      return std::nullopt;
    const QByteArray payload = file.readAll();
    if (file.error() != QFileDevice::NoError)
      return std::nullopt;

    body += "--";
    body += boundary;
    body += "\r\nContent-Disposition: form-data; name=\"";
    body += toWire(part.name);
    body += "\"; filename=\"";
    body += toWire(QFileInfo(part.path).fileName());
    body += "\"";
    if (!part.mimeType.isEmpty()) {
      body += "\r\nContent-Type: ";
      body += part.mimeType.toUtf8();
    }
    body += "\r\n\r\n";
    body += payload;
    body += "\r\n";
  }

  body += "--";
  body += boundary;
  body += "--\r\n";
  return body;
}

} // namespace ls
