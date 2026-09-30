// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settings.h"
#include "app/singleinstance.h"
#include "app/translation.h"
#include "app/trayapp.h"

#include <QApplication>
#include <QGuiApplication>

#ifndef LS_VERSION
#define LS_VERSION "1.0.0"
#endif

int main(int argc, char** argv)
{
  // Must match the installed desktop file's name. The portals use it as our app id; without it
  // KDE files our global shortcuts under whatever terminal launched us.
  QGuiApplication::setDesktopFileName(QStringLiteral("io.github.pawslaves.Tinyshot"));

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Tinyshot"));
  QApplication::setApplicationDisplayName(QStringLiteral("Tinyshot"));
  QApplication::setOrganizationName(QStringLiteral("Tinyshot"));
  QApplication::setOrganizationDomain(QStringLiteral("io.github.pawslaves"));
  QApplication::setApplicationVersion(QStringLiteral(LS_VERSION));
  // We live in the tray, so closing a dialog must not quit.
  QApplication::setQuitOnLastWindowClosed(false);

  // The language has to be in place before the first widget is created.
  ls::installTranslator(ls::Settings::instance().value(QStringLiteral("Language")).toString());

  ls::SingleInstance instance;
  if (!instance.acquire())
    return 0; // Tinyshot is already running

  ls::TrayApp tray;
  tray.init();
  return app.exec();
}
