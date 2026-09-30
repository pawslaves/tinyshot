// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace ls {

class PrntscrApi : public QObject {
  Q_OBJECT
public:
  explicit PrntscrApi(QObject* parent = nullptr);
  ~PrntscrApi() override;

  // Each sends one request and answers through the matching signal. Calls made while a request
  // is still pending are ignored.
  void attachApplication();
  void detachApplication();
  void getUser();

  // Creates a sign-in token and returns the prntscr.com page where the user approves it.
  // attachApplication() then succeeds once they have; the caller polls it (Lightshot tried every
  // 5 s, 20 times).
  QString beginSignIn();
  QString signInUrl() const;

signals:
  void attachDone(bool ok);
  void detachDone(bool ok);
  void userChanged(const QString& name, const QString& id);

private:
  struct Private;
  std::unique_ptr<Private> d;
};

} // namespace ls
