// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

#include <memory>

class QFile;

namespace ls {

class SingleInstance : public QObject {
  Q_OBJECT
public:
  explicit SingleInstance(QObject* parent = nullptr);
  ~SingleInstance() override;

  // Returns false when another instance already holds the lock; the caller must then exit
  // silently - nothing is forwarded to the running instance.
  bool acquire(const QString& name = QStringLiteral("tinyshot"));
  bool isHeld() const;

private:
  // The lock file used outside sandboxes (QLockFile: content and mtime decide whether a lock
  // left behind by a dead process is stale).
  std::unique_ptr<QLockFile> m_lock;
  // Under Flatpak: the shared lock file in $XDG_RUNTIME_DIR/app/<app-id>/, held with flock().
  // An open file is what keeps it locked; the process exiting releases it.
  std::unique_ptr<QFile> m_sandboxLock;
};

} // namespace ls
