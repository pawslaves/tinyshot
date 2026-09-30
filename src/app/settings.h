// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

namespace ls {

class Settings {
public:
  static Settings& instance();

  QVariant value(const QString& key) const;          // returns the built-in default when unset
  void setValue(const QString& key, const QVariant& v);

  // typed accessors for the common keys:
  bool autoCopy() const; bool autoClose() const; bool showBubbles() const;
  bool keepSelection() const; bool captureCursor() const;
  bool uploadEnabled() const;   // "UploadEnabled": uploads are opt-in, off by default
  int format() const;        // 1 PNG, 2 JPEG, 3 BMP
  int uploadFormat() const;  // 1 PNG, 2 JPEG
  int jpegQuality() const;   // 50..100, default 90
  QString lastSavedDir() const; void setLastSavedDir(const QString&);
  int proxyType() const; QString proxyString() const;
  QString token() const; void setToken(const QString&);
  QString appId() const;     // generated on first use: braced, upper-case UUID, persisted
  QString userName() const; QString userId() const;
  void setUser(const QString& name, const QString& id);

  // Shown once, when uploads are switched on for the first time.
  bool uploadNoticeShown() const;
  void setUploadNoticeShown();

  // Hotkeys, account state and first-run helpers.

  enum HotkeyId { HotkeyMain = 0, HotkeySaveFull = 1, HotkeyUploadFull = 2 };

  // MOD_* bits (1 Alt, 2 Ctrl, 4 Shift, 8 Win) plus a VK code (44 = VK_SNAPSHOT / Print Screen).
  struct Hotkey {
    bool enabled = false;
    int mod = 0;
    int vk = 0;
  };

  Hotkey hotkey(HotkeyId id) const;
  void setHotkey(HotkeyId id, const Hotkey& hk);

  bool signedIn() const;             // a non-empty stored username means signed in

  bool firstRun() const;             // appFirstRun == 1: the first-run balloon is still to be shown
  void clearFirstRun();
  void seedFirstRunDefaults();       // seeds the hotkey-enable values a fresh install carries

  // Deletes a value (used to clear the account keys on sign-out).
  void remove(const QString& key);
  void sync();

private:
  Settings();
  Settings(const Settings&) = delete;
  Settings& operator=(const Settings&) = delete;

  mutable QSettings m_settings;
};

} // namespace ls
