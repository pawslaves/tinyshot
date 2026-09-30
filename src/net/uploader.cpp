// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/uploader.h"

#include "net/httpclient.h"
#include "net/multipart.h"
#include "net/signature.h"
#include "net/tagresponse.h"

#include "../app/settings.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

namespace ls {
namespace {

constexpr int kThumbnailSide = 90;    // aspect-fit square thumbnail side
constexpr int kThumbnailQuality = 90; // JPEG quality of the thumbnail

// Lightshot tried two upload engines in turn, so a failed upload is retried once.
constexpr int kMaxAttempts = 2;

struct ImageEncoding {
  QByteArray format;    // Qt image format passed to QImage::save
  QByteArray extension; // temp-file extension (upload format 1 PNG / 2 JPEG / 3 BMP)
};

ImageEncoding encodingForUploadFormat(int uploadFormat)
{
  switch (uploadFormat) {
  case 2:
    return {QByteArrayLiteral("JPEG"), QByteArrayLiteral("jpg")};
  case 3:
    return {QByteArrayLiteral("BMP"), QByteArrayLiteral("bmp")};
  default: // 1 = PNG, the default upload format
    return {QByteArrayLiteral("PNG"), QByteArrayLiteral("png")};
  }
}

// 90x90 JPEG, aspect-fit on a white background at quality 90.
bool writeThumbnail(const QImage& image, const QString& path)
{
  const QImage scaled =
      image.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QImage canvas(kThumbnailSide, kThumbnailSide, QImage::Format_RGB32);
  canvas.fill(Qt::white);
  {
    QPainter painter(&canvas);
    painter.drawImage((kThumbnailSide - scaled.width()) / 2,
                      (kThumbnailSide - scaled.height()) / 2, scaled);
  }
  return canvas.save(path, "JPEG", kThumbnailQuality);
}

QString describeFailure(const HttpResult& result, const UploadResponse& response)
{
  if (!result.transportOk())
    return result.errorString.isEmpty() ? QStringLiteral("network error") : result.errorString;
  if (result.status != 200)
    return QStringLiteral("HTTP %1").arg(result.status);
  if (!response.statusSuccess)
    return QStringLiteral("the server did not report success");
  if (!response.shareIsHttpUrl)
    return QStringLiteral("the reply carried no http share URL");
  return QStringLiteral("upload failed");
}

} // namespace

struct Uploader::UploadState {
  // Temp files live in a private 0700 directory so other users can't read the screenshot or
  // race us for the file names.
  std::unique_ptr<QTemporaryDir> tempDir;
  QString imagePath;
  QString thumbPath;
  QList<MultipartTextPart> textParts;
  QList<MultipartFilePart> fileParts;
  QUrl url;
  ShareMode mode = ShareMode::Upload;
  int attempt = 0;
  std::unique_ptr<HttpClient> http;
};

Uploader::Uploader(QObject* parent) : QObject(parent) {}

Uploader::~Uploader() = default;

void Uploader::upload(const QImage& image, ShareMode mode, qreal dpi)
{
  // One upload at a time: a second call supersedes the transfer in flight (temporary files
  // removed, transfer aborted, no finished() for it).
  cancelUpload();

  // finished() is queued, so a caller connecting right after upload() cannot miss an
  // early failure.
  const auto failEarly = [this](const QString& message) {
    UploadResult result;
    result.error = message;
    QMetaObject::invokeMethod(
        this, [this, result] { emit finished(result); }, Qt::QueuedConnection);
  };

  if (image.isNull()) {
    failEarly(QStringLiteral("nothing to upload"));
    return;
  }

  auto state = std::make_unique<UploadState>();
  state->mode = mode;

  // Upload format and JPEG quality come from the settings the Options dialog writes.
  const Settings& settings = Settings::instance();
  const ImageEncoding encoding = encodingForUploadFormat(settings.uploadFormat());
  state->tempDir = std::make_unique<QTemporaryDir>(
      QDir::tempPath() + QStringLiteral("/lgtXXXXXX"));
  if (!state->tempDir->isValid()) {
    failEarly(QStringLiteral("cannot create the temporary image file"));
    return;
  }
  const QString stem = QFileInfo(state->tempDir->path()).fileName();
  state->imagePath = state->tempDir->filePath(
      stem + QLatin1Char('.') + QString::fromLatin1(encoding.extension));
  QFile imageFile(state->imagePath);
  if (!imageFile.open(QIODevice::WriteOnly)) {
    failEarly(QStringLiteral("cannot create the temporary image file"));
    return;
  }
  const int quality = encoding.format == QByteArrayLiteral("JPEG")
                          ? qBound(50, settings.jpegQuality(), 100)
                          : -1;
  if (!image.save(&imageFile, encoding.format.constData(), quality)) {
    failEarly(QStringLiteral("cannot encode the screenshot"));
    return;
  }
  // Flush the encoder output before the multipart writer reads the file back.
  imageFile.close();

  state->thumbPath = state->imagePath + QStringLiteral("_thumb.jpg");
  if (!writeThumbnail(image, state->thumbPath)) {
    failEarly(QStringLiteral("cannot write the thumbnail"));
    return;
  }

  // Upload URL: the unix time and the signature are both part of the path.
  const qint64 unixTime = QDateTime::currentSecsSinceEpoch();
  const QByteArray signature = uploadSignature(QString::fromLatin1(kPrntscrAppToken), unixTime);
  state->url = QUrl(QStringLiteral("https://upload.prntscr.com/upload/%1/%2/")
                        .arg(unixTime)
                        .arg(QString::fromLatin1(signature)));

  // Text parts go before the file parts on the wire, so the two lists are kept apart here.
  if (mode != ShareMode::Upload)
    state->textParts.append({QStringLiteral("direct_link"), QStringLiteral("True")});
  state->textParts.append({QStringLiteral("width"), QString::number(image.width())});
  state->textParts.append({QStringLiteral("height"), QString::number(image.height())});
  state->textParts.append({QStringLiteral("dpi"), QString::asprintf("%f", dpi)});
  const QString appToken = settings.token();
  if (!appToken.isEmpty()) // app_token is omitted entirely when empty
    state->textParts.append({QStringLiteral("app_token"), appToken});
  state->textParts.append({QStringLiteral("app_id"), settings.appId()});
  state->fileParts.append(
      {QStringLiteral("image"), state->imagePath, QStringLiteral("application/octet-stream")});
  state->fileParts.append(
      {QStringLiteral("thumb"), state->thumbPath, QStringLiteral("application/octet-stream")});

  m_state = std::move(state);
  startAttempt();
}

void Uploader::startAttempt()
{
  if (!m_state)
    return;
  ++m_state->attempt;

  // We may be inside the previous client's finished() right now, so it can't be deleted yet.
  // Disconnect it first so its cancellation isn't mistaken for this attempt's answer.
  if (HttpClient* previous = m_state->http.release()) {
    previous->disconnect(this);
    previous->deleteLater();
  }
  m_state->http = std::make_unique<HttpClient>();
  connect(m_state->http.get(), &HttpClient::progress, this, &Uploader::progress);
  connect(m_state->http.get(), &HttpClient::finished, this, &Uploader::handleAttemptResult);
  m_state->http->postMultipart(m_state->url, m_state->textParts, m_state->fileParts);
}

void Uploader::handleAttemptResult(const HttpResult& result)
{
  if (!m_state)
    return;

  // Any failed attempt (transport, status != 200, no "success", no http share) triggers the
  // one retry; only the second failure is final.
  UploadResponse response;
  if (result.transportOk() && result.status == 200)
    response = parseUploadResponse(QString::fromUtf8(result.body));
  const bool ok = result.transportOk() && result.status == 200 && response.ok();

  if (!ok && m_state->attempt < kMaxAttempts) {
    startAttempt();
    return;
  }

  UploadResult out;
  out.ok = ok;
  out.invalidToken = response.invalidToken;
  if (ok) {
    out.shareUrl = response.share;
    out.directUrl = response.url;
  } else {
    out.error = describeFailure(result, response);
  }

  // Both temporary files are deleted right after the reply, before the success action runs.
  cleanupTemporaryFiles();
  if (ok)
    runPostSuccessAction(out);

  // Notifications are left to whoever listens to finished().
  emit finished(out);
}

void Uploader::cancel()
{
  cancelUpload();
}

void Uploader::cancelUpload()
{
  if (!m_state)
    return;
  // As in startAttempt(): we may be inside finished(), so defer deleting the client.
  if (HttpClient* client = m_state->http.release()) {
    client->disconnect(this);
    client->deleteLater();
  }
  m_state.reset();
}

void Uploader::cleanupTemporaryFiles()
{
  if (!m_state)
    return;
  m_state->tempDir.reset();
  m_state->imagePath.clear();
  m_state->thumbPath.clear();
}

// What happens after a successful upload. Share links go to the clipboard and to
// Twitter/Facebook/VK; Google image search and Pinterest's media parameter need the direct link.
void Uploader::runPostSuccessAction(const UploadResult& result)
{
  const QString share = result.shareUrl;
  const QString direct = result.directUrl;

  QString target;
  switch (m_state->mode) {
  case ShareMode::Upload:
    // Plain upload: copy the link if AutoCopy is on. The progress dialog shows the result.
    if (Settings::instance().autoCopy())
      QGuiApplication::clipboard()->setText(share);
    return;
  case ShareMode::Twitter:
    target = QStringLiteral("https://twitter.com/home?source=Tinyshot&status=") + share
             + QStringLiteral("%20");
    break;
  case ShareMode::Facebook:
    target = QStringLiteral("https://www.facebook.com/sharer.php?u=") + share;
    break;
  case ShareMode::GoogleSearch:
    target = QStringLiteral("https://www.google.com/searchbyimage?image_url=") + direct;
    break;
  case ShareMode::VK:
    target = QStringLiteral("https://vk.com/share.php?url=") + share;
    break;
  case ShareMode::Pinterest:
    target = QStringLiteral("https://pinterest.com/pin/create/button/?url=") + share
             + QStringLiteral("&media=") + direct;
    break;
  }

  // Opening the URL is all that happens here; the caller closes its own window.
  QDesktopServices::openUrl(QUrl(target, QUrl::TolerantMode));
}

} // namespace ls
