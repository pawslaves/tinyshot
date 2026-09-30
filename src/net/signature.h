// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

namespace ls {

// Application token the upload endpoint expects; Lightshot stores it encrypted and decrypts it
// at run time.  It is the same for every installation.
inline constexpr char kPrntscrAppToken[] = "5CE3DF4D45AC";

// Lower-case hex MD5 (32 characters), the form the upload signature uses.
QByteArray md5Hex(QByteArrayView data);

// Matches Lightshot's client: the signature is the MD5 of the ASCII bytes of
// "<token>*<unixtime>", not of its UTF-16 encoding.
QByteArray uploadSignature(const QString& token, qint64 unixTime);

} // namespace ls
