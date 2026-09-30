// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/signature.h"

#include <QCryptographicHash>

namespace ls {

QByteArray md5Hex(QByteArrayView data)
{
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(data);
  return hash.result().toHex();
}

QByteArray uploadSignature(const QString& token, qint64 unixTime)
{
  const QString text = token + QLatin1Char('*') + QString::number(unixTime);
  // Lightshot hashes the ANSI bytes of this string; it is pure ASCII, so UTF-8 is identical.
  return md5Hex(QByteArrayView(text.toUtf8()));
}

} // namespace ls
