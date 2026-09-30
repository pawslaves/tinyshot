// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/trayapp.h"

#include "app/hotkeyportal.h"
#include "app/optionsdialog.h"
#include "app/settings.h"
#include "app/translation.h"
#include "capture/capturecontroller.h"
#include "net/prntscrapi.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ls {

namespace {

// The tray sits on panels of every shade, so it gets the symbolic variant; the coloured icon is
// used where the application presents itself (the About box).
QString appIconPath()
{
  return QStringLiteral(":/icons/io.github.pawslaves.Tinyshot.svg");
}

QString trayIconPath()
{
  return QStringLiteral(":/icons/io.github.pawslaves.Tinyshot-symbolic.svg");
}

QString balloonTitle()
{
  return QStringLiteral("Tinyshot");
}

} // namespace

TrayApp::TrayApp(QObject* parent)
  : QObject(parent)
  , m_menu(std::make_unique<QMenu>())
{
}

TrayApp::~TrayApp()
{
  if (m_hotkeys)
    m_hotkeys->unregisterHotkeys();
  m_tray.setContextMenu(nullptr);
  m_tray.hide();
}

void TrayApp::init()
{
  Settings& settings = Settings::instance();

  const bool firstRun = settings.firstRun();
  if (firstRun)
    settings.seedFirstRunDefaults();

  m_controller = std::make_unique<CaptureController>(this);
  connect(m_controller.get(), &CaptureController::balloon, this, &TrayApp::onBalloon);
  connect(m_controller.get(), &CaptureController::optionsRequested, this,
          &TrayApp::onOptionsRequested);

  m_api = std::make_unique<PrntscrApi>();
  connect(m_api.get(), &PrntscrApi::attachDone, this, &TrayApp::onAttachDone);
  connect(m_api.get(), &PrntscrApi::detachDone, this, &TrayApp::onDetachDone);
  connect(m_api.get(), &PrntscrApi::userChanged, this, &TrayApp::onUserChanged);

  m_tray.setIcon(QIcon(trayIconPath()));
  m_tray.setToolTip(QStringLiteral("Tinyshot"));
  connect(&m_tray, &QSystemTrayIcon::activated, this, &TrayApp::onTrayActivated);
  connect(&m_tray, &QSystemTrayIcon::messageClicked, this, &TrayApp::onMessageClicked);
  m_tray.setContextMenu(m_menu.get());
  rebuildMenu();
  if (!QSystemTrayIcon::isSystemTrayAvailable())
    qWarning("Tinyshot: no system tray available - the icon cannot be shown "
             "(hotkeys and the single-instance lock still work)");
  m_tray.show();

  m_hotkeys = std::make_unique<HotkeyPortal>(this);
  connect(m_hotkeys.get(), &HotkeyPortal::triggered, this, &TrayApp::onHotkeyTriggered);
  connect(m_hotkeys.get(), &HotkeyPortal::registrationFailed, this,
          &TrayApp::onHotkeyRegistrationFailed);
  m_hotkeys->registerHotkeys();
  updateTooltip();

  // appFirstRun == 1: announce the hotkey with the tooltip text, then clear the flag.
  if (firstRun) {
    showBalloon(balloonTitle(), m_tray.toolTip());
    settings.clearFirstRun();
  }

  // A stored account is queried once at startup, then refreshed on a 60 s timer, at most twice.
  // The account belongs to the upload service, so nothing happens while uploads are off.
  if (settings.uploadEnabled() && settings.signedIn()) {
    m_api->getUser();
    armUserRefreshTimer();
  }
}

void TrayApp::rebuildMenu()
{
  // With uploads enabled the account block comes first (Sign In..., or Sign Out plus "My gallery
  // (%username%)"), then the separator and the five static items. The account items belong to the
  // upload service, so the block disappears while uploads are off.
  m_menu->clear();
  const Settings& settings = Settings::instance();

  if (settings.uploadEnabled()) {
    if (settings.signedIn()) {
      m_menu->addAction(tr("My gallery (%1)").arg(settings.userName()), this, &TrayApp::onGallery);
      m_menu->addAction(tr("Sign Out"), this, &TrayApp::onSignOut);
    } else {
      m_menu->addAction(tr("Sign In..."), this, &TrayApp::onSignIn);
    }
    m_menu->addSeparator();
  }

  m_menu->addAction(tr("Take a screenshot"), this, &TrayApp::onTakeScreenshot);
  m_menu->addAction(tr("Options"), this, [this] { showOptions(0); });
  m_menu->addAction(tr("About"), this, &TrayApp::onAbout);
  m_menu->addAction(tr("Help"), this, &TrayApp::onHelp);
  m_menu->addAction(tr("Exit"), this, &TrayApp::onExit);
}

void TrayApp::updateTooltip()
{
  // Plain "Tinyshot" unless the hotkeys registered, in which case the localized text with the
  // main hotkey filled in.
  if (!m_hotkeys || !m_hotkeys->hotkeysOk()) {
    m_tray.setToolTip(QStringLiteral("Tinyshot"));
    return;
  }
  const Settings::Hotkey main = Settings::instance().hotkey(Settings::HotkeyMain);
  m_tray.setToolTip(
      tr("Press the \"%1\" key on your keyboard to take a screenshot")
          .arg(HotkeyPortal::formatHotkeyText(main.mod, main.vk)));
}

void TrayApp::showBalloon(const QString& title, const QString& text)
{
  if (!QSystemTrayIcon::supportsMessages())
    return;
  m_tray.showMessage(title, text, QSystemTrayIcon::Information, 10000);
}

void TrayApp::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
  switch (reason) {
  case QSystemTrayIcon::Trigger:
    onTakeScreenshot();
    break;
  case QSystemTrayIcon::DoubleClick:
    // Same as clicking the balloon: the notification target opens.
    onMessageClicked();
    break;
  default:
    // The context menu is popped up by Qt itself (setContextMenu).
    break;
  }
}

void TrayApp::onMessageClicked()
{
  // Open the target of the last notification; when a hotkey failed to register, also open the
  // options dialog (General page).
  if (!m_pendingFolder.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_pendingFolder));
    m_pendingFolder.clear();
  } else if (!m_pendingOpenUrl.isEmpty()) {
    QDesktopServices::openUrl(QUrl(m_pendingOpenUrl));
    m_pendingOpenUrl.clear();
  }
  if (m_hotkeyFailed) {
    m_hotkeyFailed = false;
    showOptions(0);
  }
}

void TrayApp::onBalloon(const QString& title, const QString& text, const QString& url)
{
  // Every balloon replaces the click target of the previous one, even when it carries none: a
  // click must never open the older link or folder.
  m_pendingOpenUrl.clear();
  m_pendingFolder.clear();
  if (!url.isEmpty()) {
    if (url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://")))
      m_pendingOpenUrl = url;
    else
      m_pendingFolder = url;
  }
  if (!Settings::instance().showBubbles())
    return; // ShowBubbles gates the copy/save balloons only
  showBalloon(title, text);
}

void TrayApp::onOptionsRequested()
{
  // Command 4 of the capture entry point means "show options" and opens the Proxy page.
  showOptions(3);
}

void TrayApp::showOptions(int page)
{
  if (m_optionsOpen) {
    if (m_optionsDialog)
      m_optionsDialog->raise();
    return;
  }
  if (!m_hotkeys)
    return;
  m_optionsOpen = true;

  // The sheet can change the hotkeys: unregister them first, re-register and refresh the tooltip
  // when it closes.
  m_hotkeys->unregisterHotkeys();
  updateTooltip();

  // Modeless, so a capture started from the tray menu still works while the sheet is open.  It
  // deletes itself on close and the outcome is applied by the finished() handler below.
  const bool uploadsWere = Settings::instance().uploadEnabled();
  auto* dialog = new OptionsDialog(page);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_optionsDialog = dialog;
  connect(dialog, &QDialog::finished, this, [this, dialog, uploadsWere](int result) {
    Settings& settings = Settings::instance();
    const bool languageChanged = result == QDialog::Accepted && dialog->languageChanged();
    const bool uploadsNow = settings.uploadEnabled();
    m_optionsDialog = nullptr;
    m_optionsOpen = false;

    if (languageChanged) {
      // The new language applies to everything created from now on: the tray menu and the tooltip
      // are rebuilt, the open dialogs keep the strings they were built with.
      installTranslator(settings.value(QStringLiteral("Language")).toString());
    }
    if (uploadsNow != uploadsWere) {
      // The account block and the refresh timer belong to the upload service: they follow the
      // switch right away.
      if (uploadsNow) {
        if (settings.signedIn()) {
          m_api->getUser();
          armUserRefreshTimer();
        }
      } else {
        m_userRefreshTimer.stop();
      }
    }
    if (languageChanged || uploadsNow != uploadsWere)
      rebuildMenu();
    m_hotkeys->registerHotkeys();
    updateTooltip();
  });
  dialog->show();
}

void TrayApp::onHotkeyTriggered(int command)
{
  if (m_controller)
    m_controller->makeScreenshot(command);
}

void TrayApp::onHotkeyRegistrationFailed(const QStringList& names)
{
  if (names.isEmpty())
    return;
  // Shown regardless of ShowBubbles: a failed registration must be visible.
  m_hotkeyFailed = true;
  const QString text =
      tr("Failed to register a hotkey:") + QLatin1Char('\n') + names.join(QLatin1Char('\n')) +
      QLatin1Char('\n') +
      tr("Likely another application is already using it. Click on this message to open the "
         "options dialog box.");
  showBalloon(balloonTitle(), text);
  updateTooltip();
}

void TrayApp::onTakeScreenshot()
{
  if (m_controller)
    m_controller->makeScreenshot(0);
}

void TrayApp::onSignIn()
{
  // The sign-in item is hidden while uploads are off; the guard covers the moment in between.
  if (!Settings::instance().uploadEnabled())
    return;
  if (m_signInInProgress)
    return;
  m_signInInProgress = true;

  // The account is attached in the browser; the modeless dialog below polls the API until it
  // takes.  It outlives this call, so it and the poll state live on the heap.
  const QString url = m_api->beginSignIn();
  if (!url.isEmpty())
    QDesktopServices::openUrl(QUrl(url));

  auto* progress = new QDialog;
  progress->setWindowTitle(tr("Sign In"));
  progress->setAttribute(Qt::WA_DeleteOnClose);
  auto* layout = new QVBoxLayout(progress);
  auto* bar = new QProgressBar(progress);
  bar->setRange(0, 100);
  layout->addWidget(bar);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, progress);
  connect(buttons, &QDialogButtonBox::rejected, progress, &QDialog::reject);
  layout->addWidget(buttons);

  constexpr int kMaxAttempts = 20;
  constexpr int kPollIntervalMs = 5000;
  auto* pollTimer = new QTimer(progress);
  pollTimer->setInterval(kPollIntervalMs);
  auto attempts = std::make_shared<int>(0);
  auto attempt = [this, progress, bar, pollTimer, attempts] {
    if (*attempts >= kMaxAttempts) {
      pollTimer->stop();
      progress->done(QDialog::Rejected); // out of attempts: the error box follows
      return;
    }
    ++*attempts;
    bar->setValue(*attempts * 100 / kMaxAttempts);
    m_api->attachApplication();
  };
  connect(pollTimer, &QTimer::timeout, this, attempt);

  m_signInDialog = progress;
  connect(progress, &QDialog::finished, this, [this, pollTimer](int result) {
    pollTimer->stop();
    m_signInDialog = nullptr;
    m_signInInProgress = false;

    if (result != QDialog::Accepted) {
      // Cancelled, or the attempts ran out: show the sign-in error box.
      showSignInError();
      rebuildMenu();
      return;
    }

    m_signInPendingBalloon = true;
    m_api->getUser(); // the username/userid of the freshly attached account
    rebuildMenu();
    if (!Settings::instance().userName().isEmpty()) {
      showSignedInBalloon();
      m_signInPendingBalloon = false;
    }
    armUserRefreshTimer();
  });

  attempt(); // send the first request right away, then let the timer poll
  pollTimer->start();
  progress->show();
}

void TrayApp::onSignOut()
{
  // The account items are hidden while uploads are off; the guard covers the moment in between.
  if (!Settings::instance().uploadEnabled())
    return;
  // PrntscrApi clears the stored account immediately, before the detach POST, so the menu can be
  // rebuilt right away.
  m_api->detachApplication();
  rebuildMenu();
}

void TrayApp::onGallery()
{
  const QString userId = Settings::instance().userId();
  QDesktopServices::openUrl(
      QUrl(QStringLiteral("https://prntscr.com/gallery.html#id=") + userId));
}

void TrayApp::onHelp()
{
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/pawslaves/tinyshot#readme")));
}

void TrayApp::onAbout()
{
  showAboutDialog();
}

void TrayApp::onExit()
{
  QCoreApplication::quit();
}

void TrayApp::onAttachDone(bool ok)
{
  // A failed attempt only means "not attached yet": the dialog keeps polling and the error box is
  // shown when the user cancels or the attempts run out.  Only success closes the dialog.
  if (!ok)
    return;
  if (m_signInDialog) {
    m_signInDialog->done(QDialog::Accepted);
    return;
  }
  // The dialog is already gone (the user cancelled while the request was in flight): the account
  // state still changed, so refresh the menu.
  rebuildMenu();
}

void TrayApp::onDetachDone(bool ok)
{
  Q_UNUSED(ok); // the detach result has no UI of its own; the account state is already cleared
  rebuildMenu();
}

void TrayApp::onUserChanged(const QString& name, const QString& id)
{
  Settings& settings = Settings::instance();
  // Only overwrite on a real answer: an empty name means the lookup failed, not a sign-out.
  if (!name.isEmpty() && (settings.userName() != name || settings.userId() != id))
    settings.setUser(name, id);
  rebuildMenu();
  if (m_signInPendingBalloon && !name.isEmpty()) {
    showSignedInBalloon();
    m_signInPendingBalloon = false;
  }
}

void TrayApp::onUserRefreshTimer()
{
  // The account belongs to the upload service: a lookup must not survive the switch going off.
  if (!Settings::instance().uploadEnabled()) {
    m_userRefreshTimer.stop();
    return;
  }
  // The startup lookup is followed by at most two more, 60 s apart.
  if (m_userRefreshCount >= 2) {
    m_userRefreshTimer.stop();
    return;
  }
  ++m_userRefreshCount;
  m_api->getUser();
}

void TrayApp::armUserRefreshTimer()
{
  m_userRefreshCount = 0;
  m_userRefreshTimer.setInterval(60000);
  m_userRefreshTimer.setSingleShot(false);
  if (!m_userRefreshTimer.isActive())
    m_userRefreshTimer.start();
}

void TrayApp::showSignedInBalloon()
{
  const QString text = tr("Signed in as") + QLatin1Char(' ') + Settings::instance().userName();
  showBalloon(balloonTitle(), text);
}

void TrayApp::showSignInError()
{
  auto* box = new QMessageBox;
  box->setIcon(QMessageBox::Warning);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Tinyshot sign-in error"));
  box->setText(tr("Tinyshot: An error occurred while signing in. Please retry."));
  box->show();
}

void TrayApp::showAboutDialog()
{
  if (m_aboutDialog) {
    // Already open: bring it to the front instead of stacking a second box.
    m_aboutDialog->raise();
    m_aboutDialog->activateWindow();
    return;
  }

  auto* dialog = new QDialog;
  dialog->setWindowTitle(tr("About"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_aboutDialog = dialog;
  auto* layout = new QVBoxLayout(dialog);

  auto* logo = new QLabel(dialog);
  logo->setPixmap(QIcon(appIconPath()).pixmap(64, 64));
  logo->setAlignment(Qt::AlignCenter);
  layout->addWidget(logo);

  auto* title = new QLabel(QStringLiteral("Tinyshot"), dialog);
  QFont titleFont = title->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  auto* version = new QLabel(
      tr("version %1").arg(QCoreApplication::applicationVersion()), dialog);
  version->setAlignment(Qt::AlignCenter);
  layout->addWidget(version);

  auto* tagline = new QLabel(tr("a tiny, Lightshot-style screenshot tool"), dialog);
  tagline->setAlignment(Qt::AlignCenter);
  layout->addWidget(tagline);

  auto* maintainer =
      new QLabel(tr("Maintained by <a href=\"https://github.com/pawslaves\">pawslaves</a>"), dialog);
  maintainer->setOpenExternalLinks(true);
  maintainer->setAlignment(Qt::AlignCenter);
  layout->addWidget(maintainer);

  auto* repo = new QLabel(QStringLiteral("<a href=\"https://github.com/pawslaves/tinyshot\">"
                                         "github.com/pawslaves/tinyshot</a>"),
                          dialog);
  repo->setOpenExternalLinks(true);
  repo->setAlignment(Qt::AlignCenter);
  layout->addWidget(repo);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, dialog);
  connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  layout->addWidget(buttons);
  dialog->show();
}

} // namespace ls
