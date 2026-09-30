// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace ls {

// Selects the post-success action and whether direct_link=True is sent on the wire.
enum class ShareMode { Upload = 0, Twitter = 1, Facebook = 2, GoogleSearch = 3, VK = 4, Pinterest = 5 };

struct UploadResult {
  bool ok = false;
  QString shareUrl;     // <share> value: the short page URL shown/copied/shared
  QString directUrl;    // <url> value: the direct image link
  bool invalidToken = false; // the reply carried "<invalid_token" (independent of ok)
  QString error;        // human-readable failure reason when !ok
};

class Uploader : public QObject {
  Q_OBJECT
public:
  explicit Uploader(QObject* parent = nullptr);
  ~Uploader() override;

  // Encodes the image using the upload format settings, makes a 90x90 thumbnail, uploads both,
  // then does whatever `mode` asks for (copy the link, open a share or search page).
  void upload(const QImage& image, ShareMode mode, qreal dpi);

  // Stops the current upload and deletes its temp files. No finished() follows, so the caller
  // has to close its own progress UI.
  void cancel();

signals:
  void progress(qint64 sent, qint64 total);
  void finished(const ls::UploadResult& result);

private:
  struct UploadState;

  void startAttempt();
  void handleAttemptResult(const struct HttpResult& result);
  void cancelUpload();
  void cleanupTemporaryFiles();
  void runPostSuccessAction(const UploadResult& result);

  std::unique_ptr<UploadState> m_state;
};

} // namespace ls
