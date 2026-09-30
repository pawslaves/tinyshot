// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/httpclient.h"

#include "../app/settings.h"

#include <QCoreApplication>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ls {
namespace {

// Identify ourselves rather than sending a library default.
QByteArray userAgent()
{
  return QByteArrayLiteral("Tinyshot/") + QCoreApplication::applicationVersion().toUtf8();
}

// Aborts a transfer after this long without progress in either direction.
constexpr int kTransferTimeoutMs = 60000;

// Proxy mode values written by the Options dialog: 0 = system proxy, 1 = direct,
// 3 = explicit HTTP proxy (never SOCKS or HTTPS proxy).
constexpr int kProxySystem = 0;
constexpr int kProxyDirect = 1;
constexpr int kProxyExplicit = 3;

// Accepts plain "host:port" (what the Options dialog saves) as well as the
// "http=host:port;https=host:port" list, from which only the http entry is used.
void parseProxyString(const QString& raw, QString* host, quint16* port)
{
  *host = QString();
  *port = 0;

  QString value = raw.trimmed();
  const int httpField = value.indexOf(QStringLiteral("http="), 0, Qt::CaseInsensitive);
  if (httpField >= 0) {
    value = value.mid(httpField + 5);
    const int separator = value.indexOf(QLatin1Char(';'));
    if (separator >= 0)
      value.truncate(separator);
  }
  if (value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
    value.remove(0, 7);
  if (value.isEmpty())
    return;

  const int colon = value.lastIndexOf(QLatin1Char(':'));
  if (colon <= 0)
    return;
  QString hostPart = value.left(colon);
  const int portValue = value.mid(colon + 1).toInt();
  if (hostPart.startsWith(QLatin1Char('[')) && hostPart.endsWith(QLatin1Char(']')))
    hostPart = hostPart.mid(1, hostPart.size() - 2);
  if (hostPart.isEmpty() || portValue <= 0 || portValue > 65535)
    return;
  *host = hostPart;
  *port = static_cast<quint16>(portValue);
}

} // namespace

HttpClient::HttpClient(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
  applyProxySettings();
}

// A reply can still emit finished() while it is being torn down (abort in the reply/manager
// destructor), which would reach a half-destroyed client: drop the connections and the pointer
// before the child objects go away.
HttpClient::~HttpClient()
{
  if (m_reply != nullptr) {
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply = nullptr;
  }
}

bool HttpClient::busy() const
{
  return m_reply != nullptr;
}

void HttpClient::abort()
{
  if (m_reply != nullptr)
    m_reply->abort();
}

void HttpClient::applyProxySettings()
{
  const Settings& settings = Settings::instance();
  const int type = settings.proxyType();
  const QString proxyString = settings.proxyString();

  switch (type) {
  case kProxySystem:
    // Mode 0: use the system configuration, as WinINet did.
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_manager->setProxy(QNetworkProxy());
    break;
  case kProxyDirect:
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    m_manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    break;
  case kProxyExplicit: {
    QString host;
    quint16 port = 0;
    parseProxyString(proxyString, &host, &port);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    if (host.isEmpty() || port == 0) {
      // Proxy mode is set but no usable address: connect directly.
      m_manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
      break;
    }
    m_manager->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, host, port));
    break;
  }
  default:
    // Any other mode value means "no proxy".
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    m_manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    break;
  }
}

QNetworkRequest HttpClient::makeRequest(const QUrl& url, const HeaderList& headers) const
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  // Lightshot always sent "*/*" as the accepted types.
  request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("*/*"));
  request.setTransferTimeout(kTransferTimeoutMs);
  // Redirects are followed, as Lightshot's WinINet engine did.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  // TLS certificates are verified.  Lightshot disabled revocation checks; Qt does not do them by
  // default either.
  for (const QPair<QByteArray, QByteArray>& header : headers)
    request.setRawHeader(header.first, header.second);
  return request;
}

void HttpClient::postMultipart(const QUrl& url,
                               const QList<MultipartTextPart>& textParts,
                               const QList<MultipartFilePart>& fileParts,
                               const HeaderList& headers)
{
  if (busy()) {
    reportLocalFailure(QStringLiteral("HttpClient: a request is already in flight"));
    return;
  }

  const QByteArray boundary = randomBoundary();
  const std::optional<QByteArray> body = buildMultipartBody(boundary, textParts, fileParts);
  if (!body.has_value()) {
    reportLocalFailure(QStringLiteral("HttpClient: cannot read an upload file part"));
    return;
  }

  HeaderList allHeaders = headers;
  allHeaders.append(qMakePair(QByteArrayLiteral("Content-Type"),
                              QByteArrayLiteral("multipart/form-data; boundary=") + boundary));
  send(url, *body, allHeaders);
}

void HttpClient::post(const QUrl& url, const QByteArray& body, const HeaderList& headers)
{
  if (busy()) {
    reportLocalFailure(QStringLiteral("HttpClient: a request is already in flight"));
    return;
  }
  send(url, body, headers);
}

QNetworkReply* HttpClient::send(const QUrl& url, const QByteArray& body,
                                const HeaderList& headers)
{
  applyProxySettings();
  const QNetworkRequest request = makeRequest(url, headers);
  m_reply = m_manager->post(request, body);

  QNetworkReply* reply = m_reply;
  connect(reply, &QNetworkReply::uploadProgress, this, &HttpClient::progress);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    if (reply != m_reply) {
      // Cancelled request that was superseded by a newer one.
      reply->deleteLater();
      return;
    }
    m_reply = nullptr;

    HttpResult result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.error = reply->error() == QNetworkReply::NoError ? 0 : static_cast<int>(reply->error());
    result.errorString = reply->errorString();
    result.body = reply->readAll();
    reply->deleteLater();
    // HTTP error statuses aren't transport errors; callers check `status` themselves.
    emit finished(result);
  });
  return reply;
}

void HttpClient::reportLocalFailure(const QString& message)
{
  HttpResult result;
  result.error = static_cast<int>(QNetworkReply::UnknownNetworkError);
  result.errorString = message;
  // Queue it so callers always see the answer after post*() returned.
  QMetaObject::invokeMethod(
      this, [this, result] { emit finished(result); }, Qt::QueuedConnection);
}

} // namespace ls
