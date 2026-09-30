// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hotkeyportal.h"

#include "app/settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QGuiApplication>
#include <QSocketNotifier>
#include <QUuid>
#include <QVariant>

#ifdef LS_HAVE_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

namespace ls {

namespace {

const QLatin1String kPortalService("org.freedesktop.portal.Desktop");
const QLatin1String kPortalPath("/org/freedesktop/portal/desktop");
const QLatin1String kGlobalShortcutsIface("org.freedesktop.portal.GlobalShortcuts");
const QLatin1String kRequestIface("org.freedesktop.portal.Request");
const QLatin1String kSessionIface("org.freedesktop.portal.Session");
const QLatin1String kPropertiesIface("org.freedesktop.DBus.Properties");

constexpr int kPortalCallTimeoutMs = 20000;
// The probe runs during startup, so a hung portal gets a short leash.
constexpr int kPortalProbeTimeoutMs = 5000;

// Shortcut id, the capture command it runs (0 select a region, 1 save the whole screen, 2 upload
// the whole screen) and the key we ask for. The descriptions are shown in the desktop's shortcut
// settings.
struct ShortcutSpec {
  const char* id;
  int command;
  const char* preferredTrigger;
  const char* description;
};

const ShortcutSpec kShortcutSpecs[] = {
  {"main", 0, "Print", QT_TRANSLATE_NOOP("HotkeyPortal", "General hotkey")},
  {"save_full", 1, "SHIFT+Print",
   QT_TRANSLATE_NOOP("HotkeyPortal", "Instant save of the fullscreen")},
  {"upload_full", 2, "CTRL+Print",
   QT_TRANSLATE_NOOP("HotkeyPortal", "Instant upload of the fullscreen")},
};

QString shortcutDescription(const ShortcutSpec& spec)
{
  return QCoreApplication::translate("HotkeyPortal", spec.description);
}

// Whether the hotkey of `spec` takes part in the registration. The instant upload belongs to the
// upload service, which is off until the user enables it.
bool shortcutEnabled(const ShortcutSpec& spec)
{
  if (spec.command == 2 && !Settings::instance().uploadEnabled())
    return false;
  return Settings::instance().hotkey(static_cast<Settings::HotkeyId>(spec.command)).enabled;
}

// Values of an "a{sv}" reply arrive either as QDBusObjectPath (typed argument) or as a plain
// string (the variant demarshaller inside a dict does not always keep the object-path type, e.g.
// the portal's session_handle), so accept both forms.
QString objectPathOf(const QVariant& value)
{
  if (value.canConvert<QDBusObjectPath>()) {
    const QString path = value.value<QDBusObjectPath>().path();
    if (!path.isEmpty())
      return path;
  }
  return value.toString();
}

#ifdef LS_HAVE_X11

// XGrabKey reports "already grabbed" only as an asynchronous BadAccess error, so the grabs are
// bracketed by an error handler plus XSync to detect conflicts.
bool g_x11GrabFailed = false;

int x11ErrorHandler(Display*, XErrorEvent* event)
{
  if (event && event->error_code == BadAccess)
    g_x11GrabFailed = true;
  return 0;
}

KeySym vkToKeySym(int vk)
{
  if (vk >= 0x30 && vk <= 0x39) // 0-9
    return XK_0 + (vk - 0x30);
  if (vk >= 0x41 && vk <= 0x5A) // A-Z
    return XK_A + (vk - 0x41);
  if (vk >= 0x60 && vk <= 0x69) // numpad 0-9
    return XK_KP_0 + (vk - 0x60);
  if (vk >= 0x70 && vk <= 0x87) // F1-F24
    return XK_F1 + (vk - 0x70);

  switch (vk) {
  case 0x08: return XK_BackSpace;
  case 0x09: return XK_Tab;
  case 0x0D: return XK_Return;
  case 0x13: return XK_Pause;
  case 0x14: return XK_Caps_Lock;
  case 0x1B: return XK_Escape;
  case 0x20: return XK_space;
  case 0x21: return XK_Prior;
  case 0x22: return XK_Next;
  case 0x23: return XK_End;
  case 0x24: return XK_Home;
  case 0x25: return XK_Left;
  case 0x26: return XK_Up;
  case 0x27: return XK_Right;
  case 0x28: return XK_Down;
  case 0x2C: return XK_Print; // VK_SNAPSHOT - the default trigger
  case 0x2D: return XK_Insert;
  case 0x2E: return XK_Delete;
  case 0x6A: return XK_KP_Multiply;
  case 0x6B: return XK_KP_Add;
  case 0x6C: return XK_KP_Separator;
  case 0x6D: return XK_KP_Subtract;
  case 0x6E: return XK_KP_Decimal;
  case 0x6F: return XK_KP_Divide;
  case 0x90: return XK_Num_Lock;
  case 0x91: return XK_Scroll_Lock;
  case 0xBA: return XK_semicolon;
  case 0xBB: return XK_equal;
  case 0xBC: return XK_comma;
  case 0xBD: return XK_minus;
  case 0xBE: return XK_period;
  case 0xBF: return XK_slash;
  case 0xC0: return XK_grave;
  case 0xDB: return XK_bracketleft;
  case 0xDC: return XK_backslash;
  case 0xDD: return XK_bracketright;
  case 0xDE: return XK_apostrophe;
  default: return NoSymbol;
  }
}

unsigned int x11Modifiers(int mod)
{
  unsigned int mask = 0;
  if (mod & 1) // MOD_ALT
    mask |= Mod1Mask;
  if (mod & 2) // MOD_CONTROL
    mask |= ControlMask;
  if (mod & 4) // MOD_SHIFT
    mask |= ShiftMask;
  if (mod & 8) // MOD_WIN
    mask |= Mod4Mask;
  return mask;
}

#endif // LS_HAVE_X11

} // namespace

// ---- D-Bus marshalling of the portal's "a(sa{sv})" shortcuts argument ----

QDBusArgument& operator<<(QDBusArgument& argument, const ls::PortalShortcut& shortcut)
{
  argument.beginStructure();
  argument << shortcut.id << shortcut.properties;
  argument.endStructure();
  return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, ls::PortalShortcut& shortcut)
{
  argument.beginStructure();
  argument >> shortcut.id >> shortcut.properties;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const ls::PortalShortcutList& shortcuts)
{
  // The element metatype must be passed: Qt derives the array signature from it, which is the
  // only way to get a valid "a(sa{sv})" for an empty list.
  argument.beginArray(QMetaType::fromType<ls::PortalShortcut>());
  for (const ls::PortalShortcut& shortcut : shortcuts)
    argument << shortcut;
  argument.endArray();
  return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, ls::PortalShortcutList& shortcuts)
{
  argument.beginArray();
  while (!argument.atEnd()) {
    ls::PortalShortcut shortcut;
    argument >> shortcut;
    shortcuts.append(shortcut);
  }
  argument.endArray();
  return argument;
}

// ---- X11 fallback ----
// A private Xlib connection whose socket is watched with a QSocketNotifier, so the grab events
// arrive on the Qt event loop (no xcb dependency).

struct X11Backend {
#ifdef LS_HAVE_X11
  struct Grab {
    KeyCode code = 0;
    unsigned int mask = 0;
    int command = 0;
  };

  ~X11Backend()
  {
    if (!display)
      return;
    // Stop watching before the display goes away.
    delete notifier;
    notifier = nullptr;
    const unsigned int extras[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (const Grab& grab : grabs) {
      for (unsigned int extra : extras)
        XUngrabKey(display, grab.code, grab.mask | extra, DefaultRootWindow(display));
    }
    XSync(display, False);
    XCloseDisplay(display);
    display = nullptr;
  }

  Display* display = nullptr;
  QSocketNotifier* notifier = nullptr;
  QList<Grab> grabs;
#endif
};

HotkeyPortal::HotkeyPortal(QObject* parent)
  : QObject(parent)
{
  qDBusRegisterMetaType<PortalShortcut>();
  qDBusRegisterMetaType<PortalShortcutList>();
}

HotkeyPortal::~HotkeyPortal() = default;

QString HotkeyPortal::keyName(int vk)
{
  if (vk >= 0x30 && vk <= 0x39)
    return QString(QChar(QLatin1Char('0' + (vk - 0x30))));
  if (vk >= 0x41 && vk <= 0x5A)
    return QString(QChar(QLatin1Char('A' + (vk - 0x41))));
  if (vk >= 0x70 && vk <= 0x87)
    return QStringLiteral("F%1").arg(vk - 0x70 + 1);

  switch (vk) {
  case 0x08: return QStringLiteral("Backspace");
  case 0x09: return QStringLiteral("Tab");
  case 0x0D: return QStringLiteral("Enter");
  case 0x13: return QStringLiteral("Pause");
  case 0x14: return QStringLiteral("Caps Lock");
  case 0x1B: return QStringLiteral("Esc");
  case 0x20: return QStringLiteral("Space");
  case 0x21: return QStringLiteral("Page Up");
  case 0x22: return QStringLiteral("Page Down");
  case 0x23: return QStringLiteral("End");
  case 0x24: return QStringLiteral("Home");
  case 0x25: return QStringLiteral("Left");
  case 0x26: return QStringLiteral("Up");
  case 0x27: return QStringLiteral("Right");
  case 0x28: return QStringLiteral("Down");
  case 0x2C: return QStringLiteral("Print Screen");
  case 0x2D: return QStringLiteral("Insert");
  case 0x2E: return QStringLiteral("Delete");
  case 0x90: return QStringLiteral("Num Lock");
  case 0x91: return QStringLiteral("Scroll Lock");
  default: break;
  }
  if (vk >= 0x60 && vk <= 0x69)
    return QStringLiteral("Num %1").arg(vk - 0x60);
  switch (vk) {
  case 0x6A: return QStringLiteral("Multiply");
  case 0x6B: return QStringLiteral("Add");
  case 0x6C: return QStringLiteral("Separator");
  case 0x6D: return QStringLiteral("Subtract");
  case 0x6E: return QStringLiteral("Decimal");
  case 0x6F: return QStringLiteral("Divide");
  case 0xBA: return QStringLiteral(";");
  case 0xBB: return QStringLiteral("=");
  case 0xBC: return QStringLiteral(",");
  case 0xBD: return QStringLiteral("-");
  case 0xBE: return QStringLiteral(".");
  case 0xBF: return QStringLiteral("/");
  case 0xC0: return QStringLiteral("`");
  case 0xDB: return QStringLiteral("[");
  case 0xDC: return QStringLiteral("\\");
  case 0xDD: return QStringLiteral("]");
  case 0xDE: return QStringLiteral("'");
  default: break;
  }
  // Linux has no key-name service like GetKeyNameTextW, so unknown codes fall back to the raw
  // number.
  return QString::number(vk);
}

QString HotkeyPortal::formatHotkeyText(int mod, int vk)
{
  // "Ctrl + ", "Shift + ", "Alt + ", "Win + " in exactly this order.
  QString text;
  if (mod & 2)
    text += QStringLiteral("Ctrl + ");
  if (mod & 4)
    text += QStringLiteral("Shift + ");
  if (mod & 1)
    text += QStringLiteral("Alt + ");
  if (mod & 8)
    text += QStringLiteral("Win + ");
  if (vk != 0)
    text += keyName(vk);
  return text;
}

PortalShortcutList HotkeyPortal::enabledShortcuts() const
{
  PortalShortcutList shortcuts;
  for (const ShortcutSpec& spec : kShortcutSpecs) {
    if (!shortcutEnabled(spec))
      continue;
    PortalShortcut shortcut;
    shortcut.id = QLatin1String(spec.id);
    shortcut.properties.insert(QStringLiteral("description"), shortcutDescription(spec));
    shortcut.properties.insert(QStringLiteral("preferred_trigger"),
                               QLatin1String(spec.preferredTrigger));
    shortcuts.append(shortcut);
  }
  return shortcuts;
}

QStringList HotkeyPortal::enabledHotkeyNames() const
{
  QStringList names;
  for (const ShortcutSpec& spec : kShortcutSpecs) {
    if (shortcutEnabled(spec))
      names << shortcutDescription(spec);
  }
  return names;
}

QString HotkeyPortal::newToken()
{
  QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  token.remove(QLatin1Char('-'));
  return token;
}

QString HotkeyPortal::predictedRequestPath(const QString& token)
{
  // The portal derives the Request object path from the caller's unique name and handle_token:
  // /org/freedesktop/portal/desktop/request/<SENDER>/<TOKEN> with ':' dropped and '.' -> '_'.
  QString sender = QDBusConnection::sessionBus().baseService();
  if (sender.startsWith(QLatin1Char(':')))
    sender.remove(0, 1);
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

bool HotkeyPortal::portalAvailable() const
{
  const QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected())
    return false;
  // Don't check isServiceRegistered(): the portal is D-Bus activated and often isn't running
  // yet. This Get starts it, and only succeeds if it offers GlobalShortcuts.
  QDBusMessage message = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kPropertiesIface, QStringLiteral("Get"));
  message << QString(kGlobalShortcutsIface) << QStringLiteral("version");
  const QDBusMessage reply = bus.call(message, QDBus::Block, kPortalProbeTimeoutMs);
  return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
}

void HotkeyPortal::subscribeRequest(const QString& path, const char* slot)
{
  if (path.isEmpty())
    return;
  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.disconnect(kPortalService, path, kRequestIface, QStringLiteral("Response"), this, slot);
  // Subscribe before making the call so the Response can't arrive before we're listening. If the
  // portal hands back a different path than predicted, the caller subscribes to that one too.
  if (!bus.connect(kPortalService, path, kRequestIface, QStringLiteral("Response"), this, slot))
    qWarning("Tinyshot: could not subscribe to %s on %s", qPrintable(path), qPrintable(kPortalService));
}

void HotkeyPortal::registerHotkeys()
{
  unregisterHotkeys();
  if (portalAvailable()) {
    startPortalRegistration();
    return;
  }
  startX11Registration();
}

void HotkeyPortal::unregisterHotkeys()
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.disconnect(kPortalService, m_sessionRequestPath, kRequestIface, QStringLiteral("Response"),
                 this, SLOT(onSessionResponse(uint,QVariantMap)));
  bus.disconnect(kPortalService, m_bindRequestPath, kRequestIface, QStringLiteral("Response"),
                 this, SLOT(onBindResponse(uint,QVariantMap)));
  bus.disconnect(kPortalService, m_listRequestPath, kRequestIface, QStringLiteral("Response"),
                 this, SLOT(onListResponse(uint,QVariantMap)));
  if (!m_sessionHandle.isEmpty()) {
    bus.disconnect(kPortalService, m_sessionHandle, kSessionIface, QStringLiteral("Closed"), this,
                   SLOT(onSessionClosed()));
    bus.disconnect(kPortalService, kPortalPath, kGlobalShortcutsIface,
                   QStringLiteral("Activated"), this,
                   SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    // Release the session explicitly: the options dialog re-registers the hotkeys afterwards, and
    // a leaked session would keep its own (stale) shortcuts bound.
    QDBusMessage close = QDBusMessage::createMethodCall(kPortalService, m_sessionHandle,
                                                       kSessionIface, QStringLiteral("Close"));
    bus.call(close, QDBus::NoBlock);
  }
  m_sessionHandle.clear();
  m_sessionRequestPath.clear();
  m_listRequestPath.clear();
  m_bindRequestPath.clear();
  m_portalActive = false;
  m_hotkeysOk = false;
  m_x11.reset();
}

void HotkeyPortal::finishRegistration(const QStringList& failedNames)
{
  m_hotkeysOk = failedNames.isEmpty();
  if (!m_hotkeysOk)
    emit registrationFailed(failedNames);
}

void HotkeyPortal::startPortalRegistration()
{
  const QString token = newToken();
  m_sessionRequestPath = predictedRequestPath(token);
  subscribeRequest(m_sessionRequestPath, SLOT(onSessionResponse(uint,QVariantMap)));

  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), token);
  options.insert(QStringLiteral("session_handle_token"), newToken());

  QDBusMessage message = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("CreateSession"));
  message << options;
  const QDBusMessage reply =
      QDBusConnection::sessionBus().call(message, QDBus::Block, kPortalCallTimeoutMs);
  if (reply.type() == QDBusMessage::ErrorMessage) {
    finishRegistration(enabledHotkeyNames());
    return;
  }
  const QDBusObjectPath handle = reply.arguments().value(0).value<QDBusObjectPath>();
  if (!handle.path().isEmpty() && handle.path() != m_sessionRequestPath) {
    m_sessionRequestPath = handle.path();
    subscribeRequest(m_sessionRequestPath, SLOT(onSessionResponse(uint,QVariantMap)));
  }
}

void HotkeyPortal::onSessionResponse(uint code, const QVariantMap& results)
{
  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.disconnect(kPortalService, m_sessionRequestPath, kRequestIface, QStringLiteral("Response"),
                 this, SLOT(onSessionResponse(uint,QVariantMap)));
  if (code != 0) {
    finishRegistration(enabledHotkeyNames());
    return;
  }
  m_sessionHandle = objectPathOf(results.value(QStringLiteral("session_handle")));
  if (m_sessionHandle.isEmpty()) {
    finishRegistration(enabledHotkeyNames());
    return;
  }
  bus.connect(kPortalService, m_sessionHandle, kSessionIface, QStringLiteral("Closed"), this,
              SLOT(onSessionClosed()));
  listShortcuts();
}

// A session can only be bound once, and on KDE every BindShortcuts pops up the shortcut settings
// for confirmation. The portal remembers the shortcuts bound by earlier sessions, so the list
// decides whether binding is needed at all.
void HotkeyPortal::listShortcuts()
{
  const QString token = newToken();
  m_listRequestPath = predictedRequestPath(token);
  subscribeRequest(m_listRequestPath, SLOT(onListResponse(uint,QVariantMap)));

  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), token);

  QDBusMessage message = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("ListShortcuts"));
  message << QVariant::fromValue(QDBusObjectPath(m_sessionHandle)) << options;
  const QDBusMessage reply =
      QDBusConnection::sessionBus().call(message, QDBus::Block, kPortalCallTimeoutMs);
  if (reply.type() == QDBusMessage::ErrorMessage) {
    // Nothing to compare against, keep the old behaviour and bind straight away.
    QDBusConnection::sessionBus().disconnect(kPortalService, m_listRequestPath, kRequestIface,
                                             QStringLiteral("Response"), this,
                                             SLOT(onListResponse(uint,QVariantMap)));
    bindShortcuts();
    return;
  }
  const QDBusObjectPath handle = reply.arguments().value(0).value<QDBusObjectPath>();
  if (!handle.path().isEmpty() && handle.path() != m_listRequestPath) {
    m_listRequestPath = handle.path();
    subscribeRequest(m_listRequestPath, SLOT(onListResponse(uint,QVariantMap)));
  }
}

void HotkeyPortal::onListResponse(uint code, const QVariantMap& results)
{
  QDBusConnection::sessionBus().disconnect(kPortalService, m_listRequestPath, kRequestIface,
                                           QStringLiteral("Response"), this,
                                           SLOT(onListResponse(uint,QVariantMap)));
  if (code != 0) {
    bindShortcuts();
    return;
  }

  // The entries of "shortcuts" arrive as a QDBusArgument; qdbus_cast handles both that and a
  // plain PortalShortcutList.
  const PortalShortcutList bound =
      qdbus_cast<PortalShortcutList>(results.value(QStringLiteral("shortcuts")));
  QStringList boundIds;
  for (const PortalShortcut& shortcut : bound)
    boundIds.append(shortcut.id);

  // Bind when any enabled shortcut is missing - the first run, or uploads that were switched on
  // after the last binding. A stale entry for a now disabled hotkey does not force a binding.
  const PortalShortcutList wanted = enabledShortcuts();
  for (const PortalShortcut& shortcut : wanted) {
    if (!boundIds.contains(shortcut.id)) {
      bindShortcuts();
      return;
    }
  }
  activatePortalSession();
}

void HotkeyPortal::bindShortcuts()
{
  const PortalShortcutList shortcuts = enabledShortcuts();
  if (shortcuts.isEmpty()) {
    // Nothing enabled: the session is up, there is simply nothing to bind.
    m_portalActive = true;
    finishRegistration(QStringList());
    return;
  }

  const QString token = newToken();
  m_bindRequestPath = predictedRequestPath(token);
  subscribeRequest(m_bindRequestPath, SLOT(onBindResponse(uint,QVariantMap)));

  QVariantMap options;
  options.insert(QStringLiteral("handle_token"), token);

  QDBusMessage message = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("BindShortcuts"));
  message << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
          << QVariant::fromValue(shortcuts) << QString() << options;
  const QDBusMessage reply =
      QDBusConnection::sessionBus().call(message, QDBus::Block, kPortalCallTimeoutMs);
  if (reply.type() == QDBusMessage::ErrorMessage) {
    finishRegistration(enabledHotkeyNames());
    return;
  }
  const QDBusObjectPath handle = reply.arguments().value(0).value<QDBusObjectPath>();
  if (!handle.path().isEmpty() && handle.path() != m_bindRequestPath) {
    m_bindRequestPath = handle.path();
    subscribeRequest(m_bindRequestPath, SLOT(onBindResponse(uint,QVariantMap)));
  }
}

void HotkeyPortal::onBindResponse(uint code, const QVariantMap& results)
{
  Q_UNUSED(results); // the granted triggers, which the portal may have adjusted by user choice
  QDBusConnection::sessionBus().disconnect(kPortalService, m_bindRequestPath, kRequestIface,
                                           QStringLiteral("Response"), this,
                                           SLOT(onBindResponse(uint,QVariantMap)));
  if (code != 0) {
    m_portalActive = false;
    finishRegistration(enabledHotkeyNames());
    return;
  }
  activatePortalSession();
}

void HotkeyPortal::activatePortalSession()
{
  m_portalActive = true;
  // Activated is emitted by the portal object, not the session; onActivated filters by session.
  if (!QDBusConnection::sessionBus().connect(
          kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("Activated"), this,
          SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap))))
    qWarning("Tinyshot: could not subscribe to GlobalShortcuts::Activated");
  finishRegistration(QStringList());
}

void HotkeyPortal::onActivated(const QDBusObjectPath& sessionHandle, const QString& shortcutId,
                               qulonglong timestamp, const QVariantMap& options)
{
  if (sessionHandle.path() != m_sessionHandle)
    return;
  Q_UNUSED(timestamp);
  Q_UNUSED(options);
  for (const ShortcutSpec& spec : kShortcutSpecs) {
    if (shortcutId == QLatin1String(spec.id)) {
      emit triggered(spec.command);
      return;
    }
  }
}

void HotkeyPortal::onSessionClosed()
{
  // The user or the compositor revoked the session.  Keep running - the tray still works and the
  // tooltip falls back to the plain "Tinyshot".
  m_sessionHandle.clear();
  m_portalActive = false;
  m_hotkeysOk = false;
}

void HotkeyPortal::startX11Registration()
{
#ifdef LS_HAVE_X11
  if (QGuiApplication::platformName() != QLatin1String("xcb")) {
    startX11RegistrationUnsupported();
    return;
  }
  auto backend = std::make_unique<X11Backend>();
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    startX11RegistrationUnsupported();
    return;
  }
  backend->display = display;
  const Window root = DefaultRootWindow(display);
  const Settings& settings = Settings::instance();
  QStringList failed;
  int (*previousHandler)(Display*, XErrorEvent*) = XSetErrorHandler(x11ErrorHandler);
  for (const ShortcutSpec& spec : kShortcutSpecs) {
    const Settings::Hotkey hotkey = settings.hotkey(static_cast<Settings::HotkeyId>(spec.command));
    if (!shortcutEnabled(spec))
      continue;
    const KeySym keysym = vkToKeySym(hotkey.vk);
    const KeyCode code = keysym == NoSymbol ? 0 : XKeysymToKeycode(display, keysym);
    if (code == 0) {
      failed << shortcutDescription(spec);
      continue;
    }
    const unsigned int mask = x11Modifiers(hotkey.mod);
    // Caps Lock / Num Lock change the modifier state, so grab the four combinations.
    const unsigned int extras[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    g_x11GrabFailed = false;
    for (unsigned int extra : extras)
      XGrabKey(display, code, mask | extra, root, True, GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    if (g_x11GrabFailed) {
      failed << shortcutDescription(spec);
      continue;
    }
    backend->grabs.append({code, mask, spec.command});
  }
  XSetErrorHandler(previousHandler);

  if (!backend->grabs.isEmpty()) {
    backend->notifier = new QSocketNotifier(ConnectionNumber(display), QSocketNotifier::Read, this);
    connect(backend->notifier, &QSocketNotifier::activated, this, &HotkeyPortal::processX11Events);
    m_x11 = std::move(backend);
  }
  finishRegistration(failed);
#else
  startX11RegistrationUnsupported();
#endif
}

void HotkeyPortal::startX11RegistrationUnsupported()
{
  // Neither the portal nor X11 is available: report every enabled hotkey as failed so the tray
  // shows the "failed to register" balloon.
  finishRegistration(enabledHotkeyNames());
}

void HotkeyPortal::processX11Events()
{
#ifdef LS_HAVE_X11
  if (!m_x11 || !m_x11->display)
    return;
  Display* display = m_x11->display;
  while (XPending(display) > 0) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type != KeyPress)
      continue;
    const unsigned int state =
        event.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
    for (const X11Backend::Grab& grab : m_x11->grabs) {
      if (grab.code == event.xkey.keycode && grab.mask == state) {
        emit triggered(grab.command);
        break;
      }
    }
  }
#endif
}

bool HotkeyPortal::portalActive() const
{
  return m_portalActive;
}

bool HotkeyPortal::x11Active() const
{
#ifdef LS_HAVE_X11
  return m_x11 && m_x11->display && !m_x11->grabs.isEmpty();
#else
  return false;
#endif
}

bool HotkeyPortal::hotkeysOk() const
{
  return m_hotkeysOk;
}

} // namespace ls
