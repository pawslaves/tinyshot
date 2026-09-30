// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translation lookup. The .qm files built from translations/*.ts are embedded under :/i18n/;
// this picks the one the "Language" setting asks for (empty = the system locale).

#pragma once

#include <QString>
#include <QStringList>

namespace ls {

// Language codes of the embedded translations, e.g. "en" or "pt_BR" (sorted).
QStringList availableLanguages();

// Human-readable name of a translation code ("pt_BR" -> "Português (Brasil)"); the code itself
// when Qt has no name for it.
QString languageDisplayName(const QString& code);

// Installs the translator that fits `language`, an empty string meaning the system locale.
// Replaces the previously installed translator. Returns false when no embedded translation
// matches, in which case the English source strings are used.
bool installTranslator(const QString& language);

} // namespace ls
