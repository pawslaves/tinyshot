// SPDX-License-Identifier: GPL-3.0-or-later
//
// Global shortcuts: registers the three screenshot hotkeys through the xdg-desktop-portal
// GlobalShortcuts interface (CreateSession + ListShortcuts, then BindShortcuts for what is
// missing, plus the Activated signal), with an X11 XGrabKey fallback for sessions where the
// portal does not provide it.

#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace ls {

// One element of the portal's "a(sa{sv})" shortcuts argument.
struct PortalShortcut {
  QString id;
  QVariantMap properties;
};
using PortalShortcutList = QList<PortalShortcut>;

struct X11Backend;

class HotkeyPortal : public QObject {
  Q_OBJECT
public:
  explicit HotkeyPortal(QObject* parent = nullptr);
  ~HotkeyPortal() override;

  void registerHotkeys();
  void unregisterHotkeys();

  bool portalActive() const;
  bool x11Active() const;
  // Last registration result: when false the tray tooltip stays the plain "Tinyshot".
  bool hotkeysOk() const;

  // Modifier prefix in this exact order, then the key name ("Ctrl + Shift + Print Screen").
  static QString formatHotkeyText(int mod, int vk);

signals:
  void triggered(int command);
  // Localized names of the hotkeys that could not be registered.
  void registrationFailed(const QStringList& hotkeyNames);

private slots:
  void onSessionResponse(uint code, const QVariantMap& results);
  void onListResponse(uint code, const QVariantMap& results);
  void onBindResponse(uint code, const QVariantMap& results);
  void onSessionClosed();
  void onActivated(const QDBusObjectPath& sessionHandle, const QString& shortcutId,
                   qulonglong timestamp, const QVariantMap& options);
  void processX11Events();

private:
  static QString keyName(int vk);
  bool portalAvailable() const;
  void startPortalRegistration();
  void startX11Registration();
  void startX11RegistrationUnsupported();
  void listShortcuts();
  void bindShortcuts();
  // The session is ready for use: watch for activations and report the registration result.
  void activatePortalSession();
  PortalShortcutList enabledShortcuts() const;
  QStringList enabledHotkeyNames() const;
  void finishRegistration(const QStringList& failedNames);
  static QString newToken();
  static QString predictedRequestPath(const QString& token);
  void subscribeRequest(const QString& path, const char* slot);

  std::unique_ptr<X11Backend> m_x11;
  QString m_sessionHandle;
  QString m_sessionRequestPath;
  QString m_listRequestPath;
  QString m_bindRequestPath;
  bool m_portalActive = false;
  bool m_hotkeysOk = false;
};

// D-Bus marshalling for the portal's "a(sa{sv})" shortcut list. These live in namespace ls so
// QDBusMetaType finds them by argument-dependent lookup.
QDBusArgument& operator<<(QDBusArgument& argument, const PortalShortcut& shortcut);
const QDBusArgument& operator>>(const QDBusArgument& argument, PortalShortcut& shortcut);
QDBusArgument& operator<<(QDBusArgument& argument, const PortalShortcutList& shortcuts);
const QDBusArgument& operator>>(const QDBusArgument& argument, PortalShortcutList& shortcuts);

} // namespace ls

Q_DECLARE_METATYPE(ls::PortalShortcut)
Q_DECLARE_METATYPE(ls::PortalShortcutList)
