// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/settings.h"

#include <QHash>
#include <QUuid>

namespace ls {

namespace {

QString hotkeyStem(Settings::HotkeyId id)
{
  switch (id) {
  case Settings::HotkeyMain:
    return QStringLiteral("Hotkey_main");
  case Settings::HotkeySaveFull:
    return QStringLiteral("Hotkey_savefull");
  case Settings::HotkeyUploadFull:
    return QStringLiteral("Hotkey_uploadfull");
  }
  return QStringLiteral("Hotkey_main");
}

// Settings defaults, keyed by the names used in the store. A key missing from this table reads
// as 0/false, like an unset value.
//
//   Hotkey_main_enabled       0   seeded to 1 on first run
//   Hotkey_main_mod           0   fallback applied when mod and vk are both 0
//   Hotkey_main_vk           44   VK_SNAPSHOT / Print Screen
//   Hotkey_savefull_enabled   0   fallback: Shift+PrtScn
//   Hotkey_savefull_mod/vk  4/44
//   Hotkey_uploadfull_enabled 0   fallback: Ctrl+PrtScn
//   Hotkey_uploadfull_mod/vk 2/44
//   AutoCopy / AutoClose      0   only the value 1 enables them
//   ShowBubbles               1
//   KeepSelection             0
//   CaptureCursor             0
//   UploadEnabled             0   uploads to prnt.sc are opt-in
//   UploadNoticeShown         0   the public-upload notice has not been shown yet
//   Format                    1   PNG
//   UploadFormat              1   PNG
//   JpegQuality              90
//   ProxyType                 0   system proxy
//   ProxyString              ""
//   Language                 ""   empty => the system locale
//   username/userid/token/appId ""  empty => signed out; token and appId generated on demand
//   appFirstRun               1   "show the balloon once"; seeded on first run, see
//                                 seedFirstRunDefaults()
//   LastSavedDir             ""   empty => ~/Pictures/Tinyshot
QVariant reDefault(const QString& key)
{
  static const QHash<QString, QVariant> kDefaults {
    {QStringLiteral("Hotkey_main_mod"), 0},
    {QStringLiteral("Hotkey_main_vk"), 44},
    {QStringLiteral("Hotkey_main_enabled"), 0},
    {QStringLiteral("Hotkey_savefull_mod"), 4},
    {QStringLiteral("Hotkey_savefull_vk"), 44},
    {QStringLiteral("Hotkey_savefull_enabled"), 0},
    {QStringLiteral("Hotkey_uploadfull_mod"), 2},
    {QStringLiteral("Hotkey_uploadfull_vk"), 44},
    {QStringLiteral("Hotkey_uploadfull_enabled"), 0},
    {QStringLiteral("AutoCopy"), 0},
    {QStringLiteral("AutoClose"), 0},
    {QStringLiteral("ShowBubbles"), 1},
    {QStringLiteral("KeepSelection"), 0},
    {QStringLiteral("CaptureCursor"), 0},
    {QStringLiteral("UploadEnabled"), 0},
    {QStringLiteral("UploadNoticeShown"), 0},
    {QStringLiteral("Format"), 1},
    {QStringLiteral("UploadFormat"), 1},
    {QStringLiteral("JpegQuality"), 90},
    {QStringLiteral("ProxyType"), 0},
    {QStringLiteral("ProxyString"), QString()},
    {QStringLiteral("Language"), QString()},
    {QStringLiteral("username"), QString()},
    {QStringLiteral("userid"), QString()},
    {QStringLiteral("token"), QString()},
    {QStringLiteral("appId"), QString()},
    {QStringLiteral("appFirstRun"), 1},
    {QStringLiteral("LastSavedDir"), QString()},
  };
  return kDefaults.value(key);
}

} // namespace

Settings& Settings::instance()
{
  static Settings settings;
  return settings;
}

Settings::Settings()
  : m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("tinyshot"),
               QStringLiteral("tinyshot"))
{
}

QVariant Settings::value(const QString& key) const
{
  if (m_settings.contains(key))
    return m_settings.value(key);
  return reDefault(key);
}

void Settings::setValue(const QString& key, const QVariant& v)
{
  m_settings.setValue(key, v);
}

void Settings::remove(const QString& key)
{
  m_settings.remove(key);
}

void Settings::sync()
{
  m_settings.sync();
}

bool Settings::autoCopy() const
{
  return value(QStringLiteral("AutoCopy")).toInt() == 1;
}

bool Settings::autoClose() const
{
  return value(QStringLiteral("AutoClose")).toInt() == 1;
}

bool Settings::showBubbles() const
{
  return value(QStringLiteral("ShowBubbles")).toInt() != 0;
}

bool Settings::keepSelection() const
{
  return value(QStringLiteral("KeepSelection")).toInt() != 0;
}

bool Settings::captureCursor() const
{
  return value(QStringLiteral("CaptureCursor")).toInt() != 0;
}

bool Settings::uploadEnabled() const
{
  return value(QStringLiteral("UploadEnabled")).toInt() != 0;
}

int Settings::format() const
{
  return value(QStringLiteral("Format")).toInt();
}

int Settings::uploadFormat() const
{
  return value(QStringLiteral("UploadFormat")).toInt();
}

int Settings::jpegQuality() const
{
  const int quality = value(QStringLiteral("JpegQuality")).toInt();
  // Values outside 50..100 fall back to 90.
  if (quality < 50 || quality > 100)
    return 90;
  return quality;
}

QString Settings::lastSavedDir() const
{
  return value(QStringLiteral("LastSavedDir")).toString();
}

void Settings::setLastSavedDir(const QString& dir)
{
  setValue(QStringLiteral("LastSavedDir"), dir);
}

int Settings::proxyType() const
{
  return value(QStringLiteral("ProxyType")).toInt();
}

QString Settings::proxyString() const
{
  return value(QStringLiteral("ProxyString")).toString();
}

QString Settings::token() const
{
  return value(QStringLiteral("token")).toString();
}

void Settings::setToken(const QString& token)
{
  setValue(QStringLiteral("token"), token);
}

QString Settings::appId() const
{
  QString id = value(QStringLiteral("appId")).toString();
  if (id.isEmpty()) {
    // "{XXXXXXXX-XXXX-...}", upper-case, and persisted so every upload and profile keeps the
    // same id.
    id = QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
    m_settings.setValue(QStringLiteral("appId"), id);
  }
  return id;
}

QString Settings::userName() const
{
  return value(QStringLiteral("username")).toString();
}

QString Settings::userId() const
{
  return value(QStringLiteral("userid")).toString();
}

void Settings::setUser(const QString& name, const QString& id)
{
  setValue(QStringLiteral("username"), name);
  setValue(QStringLiteral("userid"), id);
}

bool Settings::signedIn() const
{
  // A non-empty username means signed in; sign-out clears the stored values, so empty reads as
  // signed out.
  return !userName().isEmpty();
}

bool Settings::uploadNoticeShown() const
{
  return value(QStringLiteral("UploadNoticeShown")).toInt() != 0;
}

void Settings::setUploadNoticeShown()
{
  setValue(QStringLiteral("UploadNoticeShown"), 1);
}

Settings::Hotkey Settings::hotkey(HotkeyId id) const
{
  const QString stem = hotkeyStem(id);
  Hotkey hk;
  const int rawEnabled = value(stem + QStringLiteral("_enabled")).toInt();
  // Any non-zero value enables main/savefull; uploadfull has to be exactly 1.
  hk.enabled = (id == HotkeyUploadFull) ? (rawEnabled == 1) : (rawEnabled != 0);
  hk.mod = value(stem + QStringLiteral("_mod")).toInt();
  hk.vk = value(stem + QStringLiteral("_vk")).toInt();
  if (hk.mod == 0 && hk.vk == 0) {
    switch (id) {
    case HotkeyMain:
      hk.vk = 44; // VK_SNAPSHOT
      break;
    case HotkeySaveFull:
      hk.mod = 4; // MOD_SHIFT
      hk.vk = 44;
      break;
    case HotkeyUploadFull:
      hk.mod = 2; // MOD_CONTROL
      hk.vk = 44;
      break;
    }
  }
  return hk;
}

void Settings::setHotkey(HotkeyId id, const Hotkey& hk)
{
  const QString stem = hotkeyStem(id);
  setValue(stem + QStringLiteral("_enabled"), hk.enabled ? 1 : 0);
  setValue(stem + QStringLiteral("_mod"), hk.mod);
  setValue(stem + QStringLiteral("_vk"), hk.vk);
}

bool Settings::firstRun() const
{
  return value(QStringLiteral("appFirstRun")).toInt() == 1;
}

void Settings::clearFirstRun()
{
  setValue(QStringLiteral("appFirstRun"), 0);
}

void Settings::seedFirstRunDefaults()
{
  // Enables the default hotkeys on first run; only runs while appFirstRun == 1, so a choice the
  // user made later is never overridden.
  const QLatin1String keys[] = {QLatin1String("Hotkey_main_enabled"),
                                QLatin1String("Hotkey_savefull_enabled"),
                                QLatin1String("Hotkey_uploadfull_enabled")};
  for (const QLatin1String& key : keys) {
    if (!m_settings.contains(key))
      m_settings.setValue(key, 1);
  }
  sync();
}

} // namespace ls
