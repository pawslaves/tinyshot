// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multipart/form-data body writer.  The field layout and boundary format match what Lightshot
// sends.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace ls {

struct MultipartTextPart {
  QString name;
  QString value;
};

// File form field; the wire filename is the basename of `path`.
struct MultipartFilePart {
  QString name;
  QString path;
  QString mimeType; // empty -> no Content-Type line for this part
};

// Boundary format the upload endpoint expects: 27 dashes followed by three 4-digit upper-case
// hex fields, 39 ASCII characters in total.
QByteArray makeBoundary(quint32 r1, quint32 r2, quint32 r3);

// Boundary from three random values in 0..0x7FFF, so each hex field is four digits.
QByteArray randomBoundary();

// Body layout: all text parts in list order, then all file parts in list order, then the
// closing "--<boundary>--\r\n" line, matching Lightshot's wire order.
// Returns std::nullopt when a file part cannot be read, so an unreadable upload file is a
// hard error instead of a silently short body.
std::optional<QByteArray> buildMultipartBody(const QByteArray& boundary,
                                             const QList<MultipartTextPart>& textParts,
                                             const QList<MultipartFilePart>& fileParts);

} // namespace ls
