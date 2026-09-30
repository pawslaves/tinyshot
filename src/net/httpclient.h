// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "net/multipart.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace ls {

// Result of one request. Connection, TLS and cancellation failures set `error`; an HTTP error
// status alone does not.
struct HttpResult {
  QByteArray body;      // raw response bytes
  int status = 0;       // HTTP status code, 0 when the transfer never completed
  int error = 0;        // QNetworkReply::NetworkError value, 0 = no transport error
  QString errorString;

  bool transportOk() const { return error == 0; }
};

// Runs one request at a time and always answers it with exactly one finished(). Text is sent as
// UTF-8.
class HttpClient : public QObject {
  Q_OBJECT
public:
  using HeaderList = QList<QPair<QByteArray, QByteArray>>;

  explicit HttpClient(QObject* parent = nullptr);
  ~HttpClient() override;

  // POST with a fresh multipart body; the Content-Type header carries the generated boundary.
  void postMultipart(const QUrl& url,
                     const QList<MultipartTextPart>& textParts,
                     const QList<MultipartFilePart>& fileParts,
                     const HeaderList& headers = HeaderList());

  // POST with a raw body (used by the account JSON-RPC requests).
  void post(const QUrl& url, const QByteArray& body, const HeaderList& headers = HeaderList());

  bool busy() const;
  // Cancels the transfer in flight; the pending finished() reports the cancellation error.
  void abort();

signals:
  // Upload progress of the request body in bytes, as reported by QNetworkReply; the caller
  // derives the percentage.
  void progress(qint64 bytesSent, qint64 bytesTotal);
  void finished(const ls::HttpResult& result);

private:
  QNetworkReply* send(const QUrl& url, const QByteArray& body, const HeaderList& headers);
  QNetworkRequest makeRequest(const QUrl& url, const HeaderList& headers) const;
  void applyProxySettings();
  void reportLocalFailure(const QString& message);

  QNetworkAccessManager* m_manager;
  QNetworkReply* m_reply = nullptr;
};

} // namespace ls
