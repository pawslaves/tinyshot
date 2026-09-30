// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/tagresponse.h"

namespace ls {

QString tagValue(const QString& body, const QString& key)
{
  const QString open = QLatin1Char('<') + key + QLatin1Char('>');
  const qsizetype begin = body.indexOf(open);
  if (begin < 0)
    return QString();
  const qsizetype valueBegin = begin + open.size();
  const QString close = QStringLiteral("</") + key + QLatin1Char('>');
  const qsizetype end = body.indexOf(close, valueBegin);
  if (end < 0)
    return QString();
  return body.mid(valueBegin, end - valueBegin);
}

// Plain substring search over the whole body, not tag-aware.
bool containsInvalidToken(const QString& body)
{
  return body.contains(QStringLiteral("<invalid_token"));
}

// The scheme must sit at offset 0 (a leading space does not count), and the comparison is
// case-sensitive.
bool isHttpUrl(const QString& value)
{
  return value.startsWith(QStringLiteral("https://")) || value.startsWith(QStringLiteral("http://"));
}

UploadResponse parseUploadResponse(const QString& body)
{
  UploadResponse response;
  response.status = tagValue(body, QStringLiteral("status"));
  response.share = tagValue(body, QStringLiteral("share"));
  response.url = tagValue(body, QStringLiteral("url"));
  response.invalidToken = containsInvalidToken(body);
  // "success" anywhere inside the tag value counts, not an exact match.
  response.statusSuccess = response.status.contains(QStringLiteral("success"));
  response.shareIsHttpUrl = isHttpUrl(response.share);
  return response;
}

} // namespace ls
