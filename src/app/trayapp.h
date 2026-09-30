// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>

class QDialog;

namespace ls {

class CaptureController;
class HotkeyPortal;
class OptionsDialog;
class PrntscrApi;

class TrayApp : public QObject {
  Q_OBJECT
public:
  explicit TrayApp(QObject* parent = nullptr);
  ~TrayApp() override;

  void init();

private slots:
  void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
  void onMessageClicked();
  void onBalloon(const QString& title, const QString& text, const QString& url);
  void onOptionsRequested();
  void onHotkeyTriggered(int command);
  void onHotkeyRegistrationFailed(const QStringList& names);
  void onAttachDone(bool ok);
  void onDetachDone(bool ok);
  void onUserChanged(const QString& name, const QString& id);
  void onUserRefreshTimer();

  void onSignIn();
  void onSignOut();
  void onGallery();
  void onHelp();
  void onAbout();
  void onTakeScreenshot();
  void onExit();

private:
  void rebuildMenu();
  void updateTooltip();
  void showOptions(int page);
  void showAboutDialog();
  void showBalloon(const QString& title, const QString& text);
  void showSignInError();
  void showSignedInBalloon();
  void armUserRefreshTimer();

  QSystemTrayIcon m_tray;
  std::unique_ptr<QMenu> m_menu;

  std::unique_ptr<CaptureController> m_controller;
  std::unique_ptr<PrntscrApi> m_api;
  std::unique_ptr<HotkeyPortal> m_hotkeys;

  QTimer m_userRefreshTimer;
  int m_userRefreshCount = 0;

  QString m_pendingOpenUrl;
  QString m_pendingFolder;
  // The sign-in progress dialog while a request is in flight, plus the modeless About box and
  // options sheet while they are open.
  QPointer<QDialog> m_signInDialog;
  QPointer<QDialog> m_aboutDialog;
  QPointer<OptionsDialog> m_optionsDialog;
  bool m_hotkeyFailed = false;      // a hotkey failed to register; a tray click opens the options
  bool m_optionsOpen = false;       // re-entrancy guard: the sheet must not be opened twice
  bool m_signInInProgress = false;  // re-entrancy guard: the sign-in poll loop must not nest
  bool m_signInPendingBalloon = false;
};

} // namespace ls
