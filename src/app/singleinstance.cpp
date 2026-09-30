// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/singleinstance.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cerrno>
#include <sys/file.h>

namespace ls {

SingleInstance::SingleInstance(QObject* parent)
  : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
  if (m_lock)
    m_lock->unlock();
  if (m_sandboxLock)
    m_sandboxLock->close();  // closing the file releases the flock
}

bool SingleInstance::acquire(const QString& name)
{
  // Per-user lock directory: $XDG_RUNTIME_DIR is 0700 and belongs to the session user (Qt falls
  // back to a per-user directory when the variable is unset).  A lock file in the shared /tmp
  // could be created or removed by any other local user.
  QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty())
    dir = QDir::tempPath();

  // Every Flatpak instance gets its own /run/user/<uid>; flatpak bind-mounts
  // $XDG_RUNTIME_DIR/app/<app-id> from the host into all instances of the same app, so the lock
  // goes there.  All sandbox processes report PID 2, which defeats QLockFile's check for a lock
  // left by a dead process, so this one is an flock() the kernel drops when the process exits.
  const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
  if (!flatpakId.isEmpty()) {
    const QString appDir = QDir(dir).filePath(QStringLiteral("app/") + flatpakId);
    QDir().mkpath(appDir);
    auto file = std::make_unique<QFile>(QDir(appDir).filePath(name + QStringLiteral(".lock")));
    if (file->open(QIODevice::ReadWrite)) {
      if (::flock(static_cast<int>(file->handle()), LOCK_EX | LOCK_NB) == 0) {
        m_sandboxLock = std::move(file);
        return true;
      }
      if (errno == EWOULDBLOCK)
        return false;  // another instance holds the lock
    }
    // The shared directory is not usable; the per-session lock below is the fallback.
  }

  m_lock = std::make_unique<QLockFile>(QDir(dir).filePath(name + QStringLiteral(".lock")));
  // tryLock(0) reports "already taken" immediately instead of blocking; a lock file left behind
  // by a dead process is detected as stale and taken over.
  if (!m_lock->tryLock(0)) {
    m_lock.reset();
    return false;
  }
  return true;
}

bool SingleInstance::isHeld() const
{
  return m_sandboxLock != nullptr || (m_lock && m_lock->isLocked());
}

} // namespace ls
