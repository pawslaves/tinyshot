// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/translation.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>

namespace ls {

namespace {

const QLatin1String kResourceDir(":/i18n");
const QLatin1String kFilePrefix("tinyshot_");
const QLatin1String kFileSuffix(".qm");

// "tinyshot_pt_BR.qm" -> "pt_BR"
QString codeFromFile(const QString& fileName)
{
  QString code = fileName;
  code.remove(0, kFilePrefix.size());
  code.chop(kFileSuffix.size());
  return code;
}

// The translator installed by the last successful installTranslator() call.
QTranslator* g_translator = nullptr;

} // namespace

QStringList availableLanguages()
{
  QStringList codes;
  const QDir dir(kResourceDir);
  const QStringList files = dir.entryList({QStringLiteral("tinyshot_*.qm")}, QDir::Files);
  for (const QString& file : files)
    codes.append(codeFromFile(file));
  codes.sort();
  return codes;
}

QString languageDisplayName(const QString& code)
{
  // A code is "<language>" or "<language>_<territory>", as in the .ts file names. The names come
  // from Qt's static tables, so they read the same whatever the system locale is.
  const QStringList parts = code.split(QLatin1Char('_'), Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return code;
  const QLocale::Language language = QLocale::codeToLanguage(parts.first());
  if (language == QLocale::C || language == QLocale::AnyLanguage)
    return code;
  QString name = QLocale::languageToString(language);
  if (parts.size() > 1) {
    const QLocale::Territory territory = QLocale::codeToTerritory(parts.at(1));
    if (territory != QLocale::AnyTerritory)
      name += QStringLiteral(" (%1)").arg(QLocale::territoryToString(territory));
  }
  return name;
}

bool installTranslator(const QString& language)
{
  QCoreApplication* app = QCoreApplication::instance();
  if (!app)
    return false;

  // "" follows the system locale, trying its UI languages in order of preference; a stored code
  // is used as it is. A region-specific code also matches the bare language ("pt_BR" -> "pt").
  QStringList candidates;
  if (language.isEmpty()) {
    for (const QString& tag : QLocale::system().uiLanguages())
      candidates.append(QString(tag).replace(QLatin1Char('-'), QLatin1Char('_')));
  } else {
    candidates.append(language);
  }

  const QStringList available = availableLanguages();
  QString chosen;
  for (const QString& candidate : candidates) {
    for (const QString& code : available) {
      if (code.compare(candidate, Qt::CaseInsensitive) == 0 ||
          code.compare(candidate.section(QLatin1Char('_'), 0, 0), Qt::CaseInsensitive) == 0) {
        chosen = code;
        break;
      }
    }
    if (!chosen.isEmpty())
      break;
  }

  if (g_translator) {
    app->removeTranslator(g_translator);
    g_translator->deleteLater();
    g_translator = nullptr;
  }
  if (chosen.isEmpty())
    return false;  // no translation: the English source strings stay in place

  auto* translator = new QTranslator;
  if (!translator->load(kFilePrefix + chosen, kResourceDir)) {
    delete translator;
    return false;
  }
  g_translator = translator;
  app->installTranslator(g_translator);
  return true;
}

} // namespace ls
