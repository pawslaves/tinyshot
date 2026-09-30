// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "net/signature.h"

class TestSignature : public QObject {
  Q_OBJECT

private slots:
  void md5HexIsLowerCaseHex();
  void matchesKnownVector();
  void matchesLiveAcceptedVector();
};

void TestSignature::md5HexIsLowerCaseHex()
{
  QCOMPARE(ls::md5Hex(QByteArrayView("")), QByteArrayLiteral("d41d8cd98f00b204e9800998ecf8427e"));
  QCOMPARE(ls::md5Hex(QByteArrayView("abc")), QByteArrayLiteral("900150983cd24fb0d6963f7d28e17f72"));
  QCOMPARE(ls::md5Hex(QByteArrayView("abc")).size(), 32);
}

void TestSignature::matchesKnownVector()
{
  // Hashes the ASCII bytes of "<token>*<time>", not the UTF-16 string.
  const QByteArray signature = ls::uploadSignature(QStringLiteral("5CE3DF4D45AC"), 1759000000);
  QCOMPARE(signature, QByteArrayLiteral("4d27d196d73362c840c7288dd3fc112e"));
  QCOMPARE(signature.size(), 32);
}

void TestSignature::matchesLiveAcceptedVector()
{
  // Signature of a real upload the server accepted (https://prnt.sc/Da67AJlydsQ9).
  QCOMPARE(ls::uploadSignature(QStringLiteral("5CE3DF4D45AC"), 1790702842),
           QByteArrayLiteral("d23a13ec6e62a544e3ac44f6aecc672d"));
}

QTEST_APPLESS_MAIN(TestSignature)

#include "tst_signature.moc"
