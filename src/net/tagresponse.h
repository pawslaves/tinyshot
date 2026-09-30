// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parses the prnt.sc upload reply, which is tag text (<key>value</key>) rather than XML.
#pragma once

#include <QString>

namespace ls {

struct UploadResponse {
  QString status;             // <status> value, "" when the tag is absent
  QString share;              // <share> value: the short page URL shown/copied/shared
  QString url;                // <url> value: the direct image link
  bool invalidToken = false;  // body contains "<invalid_token"
  bool statusSuccess = false; // <status> contains "success"
  bool shareIsHttpUrl = false;// <share> starts with http:// or https://

  // True when <status> contains "success" and <share> is an http(s) URL; the transfer itself
  // is checked separately.
  bool ok() const { return statusSuccess && shareIsHttpUrl; }
};

// Text after "<key>" up to "</key>"; "" when the tag is missing.  No whitespace trimming:
// the value is used verbatim, so a leading space would break the http(s) check.
QString tagValue(const QString& body, const QString& key);

bool containsInvalidToken(const QString& body);

bool isHttpUrl(const QString& value);

UploadResponse parseUploadResponse(const QString& body);

} // namespace ls
