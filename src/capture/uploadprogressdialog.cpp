// SPDX-License-Identifier: GPL-3.0-or-later

#include "uploadprogressdialog.h"

#include "app/settings.h"
#include "net/uploader.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ls {

UploadProgressDialog::UploadProgressDialog(Uploader* uploader, QWidget* parent)
    : QDialog(parent), m_uploader(uploader)
{
  setWindowTitle(tr("Uploading image"));
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  // Not modal: a modal dialog would freeze the overlay of any new capture started while it's
  // open, and after a successful upload it stays open until the user closes it.
  setWindowModality(Qt::NonModal);
  setMinimumWidth(360);

  auto* layout = new QVBoxLayout(this);

  m_status = new QLabel(tr("Uploading image"), this);
  layout->addWidget(m_status);

  m_progress = new QProgressBar(this);
  m_progress->setRange(0, 100);
  m_progress->setValue(0);
  layout->addWidget(m_progress);

  m_link = new QLineEdit(this);
  m_link->setReadOnly(true);
  m_link->setVisible(false);
  layout->addWidget(m_link);

  m_detail = new QLabel(this);
  m_detail->setWordWrap(true);
  m_detail->setVisible(false);
  layout->addWidget(m_detail);

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);

  m_copy = new QPushButton(tr("Copy"), this);
  m_copy->setVisible(false);
  connect(m_copy, &QPushButton::clicked, this, &UploadProgressDialog::onCopyLink);
  buttons->addWidget(m_copy);

  m_open = new QPushButton(tr("Open"), this);
  m_open->setVisible(false);
  connect(m_open, &QPushButton::clicked, this, &UploadProgressDialog::onOpenLink);
  buttons->addWidget(m_open);

  m_retry = new QPushButton(tr("Retry"), this);
  m_retry->setVisible(false);
  connect(m_retry, &QPushButton::clicked, this, &UploadProgressDialog::onRetry);
  buttons->addWidget(m_retry);

  m_cancel = new QPushButton(tr("Cancel"), this);
  connect(m_cancel, &QPushButton::clicked, this, &UploadProgressDialog::onCancel);
  buttons->addWidget(m_cancel);

  layout->addLayout(buttons);

  if (m_uploader) {
    connect(m_uploader, &Uploader::progress, this, &UploadProgressDialog::onProgress);
    connect(m_uploader, &Uploader::finished, this, &UploadProgressDialog::onFinished);
  }
}

void UploadProgressDialog::start(const QImage& image, int shareMode, qreal dpr)
{
  m_image = image;
  m_shareMode = shareMode;
  m_dpr = dpr;
  setUploadingState();
  if (m_uploader)
    m_uploader->upload(m_image, static_cast<ShareMode>(m_shareMode), m_dpr);
}

void UploadProgressDialog::setUploadingState()
{
  m_state = State::Uploading;
  setWindowTitle(tr("Uploading image"));
  m_status->setText(tr("Uploading image"));
  m_detail->setVisible(false);
  m_link->setVisible(false);
  m_link->clear();
  m_copy->setVisible(false);
  m_open->setVisible(false);
  m_retry->setVisible(false);
  m_cancel->setVisible(true);
  m_progress->setVisible(true);
  m_progress->setValue(0);
}

void UploadProgressDialog::setFailureState(const QString& detail)
{
  // Offer Retry / Cancel; the proxy settings live in the Options dialog.
  m_state = State::Failure;
  m_status->setText(tr("Upload failed. Retry?"));
  if (detail.isEmpty()) {
    m_detail->setVisible(false);
  } else {
    m_detail->setText(detail);
    m_detail->setVisible(true);
  }
  m_progress->setVisible(false);
  m_link->setVisible(false);
  m_copy->setVisible(false);
  m_open->setVisible(false);
  m_retry->setVisible(true);
  m_cancel->setVisible(true);
}

void UploadProgressDialog::setSuccessState(const QString& link)
{
  m_state = State::Success;
  m_status->setText(QString());
  m_progress->setVisible(false);
  m_detail->setVisible(false);
  m_retry->setVisible(false);
  m_link->setText(link);
  m_link->setVisible(true);
  m_link->setCursorPosition(0);
  m_copy->setVisible(true);
  m_open->setVisible(!link.isEmpty());
  m_copy->setEnabled(!link.isEmpty());
  m_cancel->setVisible(true);
  m_cancel->setText(tr("Close"));
}

void UploadProgressDialog::onProgress(qint64 sent, qint64 total)
{
  if (total <= 0)
    return;
  const int percent = static_cast<int>((sent * 100) / total);
  m_progress->setValue(qBound(0, percent, 100));
}

void UploadProgressDialog::onFinished(const UploadResult& result)
{
  if (!result.ok) {
    setFailureState(result.error);
    return;
  }

  const QString link = result.shareUrl.isEmpty() ? result.directUrl : result.shareUrl;

  if (m_shareMode != static_cast<int>(ShareMode::Upload)) {
    // For share/search modes the Uploader has already opened the target URL; the window closes.
    close();
    return;
  }

  if (Settings::instance().autoCopy() && Settings::instance().autoClose()) {
    emit balloon(QStringLiteral("Tinyshot"),
                 tr("Screenshot is uploaded. Link is copied to your clipboard."), QString());
    close();
    return;
  }

  setSuccessState(link);
}

void UploadProgressDialog::cancelIfUploading()
{
  // Cancel aborts a transfer that is still in flight; a cancelled upload emits no finished().
  if (m_state == State::Uploading && m_uploader)
    m_uploader->cancel();
}

void UploadProgressDialog::onCancel()
{
  cancelIfUploading();
  close();
}

// Esc and the title-bar close button both destroy the dialog. Cancel the upload first, or its
// success action (copying the link, opening a share page) would still run afterwards.
void UploadProgressDialog::reject()
{
  cancelIfUploading();
  QDialog::reject();
}

void UploadProgressDialog::closeEvent(QCloseEvent* event)
{
  cancelIfUploading();
  QDialog::closeEvent(event);
}

void UploadProgressDialog::onRetry()
{
  setUploadingState();
  if (m_uploader)
    m_uploader->upload(m_image, static_cast<ShareMode>(m_shareMode), m_dpr);
}

void UploadProgressDialog::onCopyLink()
{
  if (QClipboard* clipboard = QGuiApplication::clipboard())
    clipboard->setText(m_link->text());
}

void UploadProgressDialog::onOpenLink()
{
  const QString link = m_link->text();
  if (link.isEmpty())
    return;
  QDesktopServices::openUrl(QUrl(link));
}

} // namespace ls
