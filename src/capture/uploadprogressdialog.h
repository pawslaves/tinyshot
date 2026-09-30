// SPDX-License-Identifier: GPL-3.0-or-later
//
// Upload progress window, driven by the Uploader's progress()/finished() signals:
//   * uploading: "Uploading image" caption, 0..100 % progress, Cancel aborts the transfer
//   * plain upload success: the link is shown with Copy/Open; with AutoCopy + AutoClose the
//     copied-link balloon is shown and the window closes, otherwise it stays up until closed
//   * share/search modes (1..5): the Uploader opens the target URL and the window closes
//   * failure: "Upload failed. Retry?" with Retry / Cancel
#pragma once

#include <QDialog>
#include <QImage>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace ls {

class Uploader;

class UploadProgressDialog : public QDialog {
  Q_OBJECT

public:
  explicit UploadProgressDialog(Uploader* uploader, QWidget* parent = nullptr);

  // Starts - or restarts, after Retry - an upload in the given share mode.
  void start(const QImage& image, int shareMode, qreal dpr);

signals:
  void balloon(const QString& title, const QString& text, const QString& url);

protected:
  // Esc and the window-manager close button must abort a transfer that is still in flight, like
  // the Cancel button does.
  void reject() override;
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onProgress(qint64 sent, qint64 total);
  void onFinished(const struct UploadResult& result);
  void onCancel();
  void onRetry();
  void onCopyLink();
  void onOpenLink();

private:
  enum class State { Uploading, Failure, Success };

  void setUploadingState();
  void setFailureState(const QString& detail);
  void setSuccessState(const QString& link);
  void cancelIfUploading();

  Uploader* m_uploader = nullptr;
  QImage m_image;
  int m_shareMode = 0;
  qreal m_dpr = 1.0;
  State m_state = State::Uploading;

  QLabel* m_status = nullptr;
  QLabel* m_detail = nullptr;
  QProgressBar* m_progress = nullptr;
  QLineEdit* m_link = nullptr;
  QPushButton* m_copy = nullptr;
  QPushButton* m_open = nullptr;
  QPushButton* m_cancel = nullptr;
  QPushButton* m_retry = nullptr;
};

} // namespace ls
