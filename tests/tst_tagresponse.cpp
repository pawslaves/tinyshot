// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the <key>value</key> upload reply parser and the success gate applied to the reply.

#include <QtTest>

#include "net/tagresponse.h"

class TestTagResponse : public QObject {
  Q_OBJECT

private slots:
  void extractsTagValues();
  void missingTagYieldsEmptyValue();
  void successBodyReportsBothUrls();
  void missingStatusIsNotSuccess();
  void statusNeedsTheLiteralSuccessSubstring();
  void invalidTokenIsFlaggedIndependently();
  void nonHttpShareIsNotSuccess();
  void shareMustStartWithTheScheme();
};

void TestTagResponse::extractsTagValues()
{
  const QString body = QStringLiteral(
      "<status>success</status><share>http://prnt.sc/abc123</share>"
      "<url>http://i.prntscr.com/abc123.png</url>");
  QCOMPARE(ls::tagValue(body, QStringLiteral("status")), QStringLiteral("success"));
  QCOMPARE(ls::tagValue(body, QStringLiteral("share")), QStringLiteral("http://prnt.sc/abc123"));
  QCOMPARE(ls::tagValue(body, QStringLiteral("url")),
           QStringLiteral("http://i.prntscr.com/abc123.png"));
}

void TestTagResponse::missingTagYieldsEmptyValue()
{
  const QString body = QStringLiteral("<status>success</status>");
  QCOMPARE(ls::tagValue(body, QStringLiteral("share")), QString());
  // A tag without its closing counterpart is treated as missing as well.
  QCOMPARE(ls::tagValue(QStringLiteral("<share>http://prnt.sc/x"), QStringLiteral("share")),
           QString());
}

void TestTagResponse::successBodyReportsBothUrls()
{
  const ls::UploadResponse response = ls::parseUploadResponse(QStringLiteral(
      "<status>success</status><share>http://prnt.sc/abc123</share>"
      "<url>http://i.prntscr.com/abc123.png</url>"));
  QVERIFY(response.ok());
  QVERIFY(response.statusSuccess);
  QVERIFY(response.shareIsHttpUrl);
  QVERIFY(!response.invalidToken);
  QCOMPARE(response.share, QStringLiteral("http://prnt.sc/abc123"));
  QCOMPARE(response.url, QStringLiteral("http://i.prntscr.com/abc123.png"));
}

void TestTagResponse::missingStatusIsNotSuccess()
{
  // A body without <status> is a failure even when a perfectly good share URL is present.
  const ls::UploadResponse response =
      ls::parseUploadResponse(QStringLiteral("<share>http://prnt.sc/abc123</share>"));
  QVERIFY(!response.ok());
  QVERIFY(!response.statusSuccess);
  QCOMPARE(response.status, QString());
}

void TestTagResponse::statusNeedsTheLiteralSuccessSubstring()
{
  // Substring test on the status value itself: "success" anywhere inside it counts,
  // nothing elsewhere in the body does.
  QVERIFY(ls::parseUploadResponse(QStringLiteral("<status>successful</status>"
                                                 "<share>http://prnt.sc/a</share>"))
              .ok());
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>upload failed</status>success"
                                                  "<share>http://prnt.sc/a</share>"))
               .ok());
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>Failure</status>"
                                                  "<share>http://prnt.sc/a</share>"))
               .ok());
}

void TestTagResponse::invalidTokenIsFlaggedIndependently()
{
  // The invalid-token flag is a plain substring search, independent of the upload result:
  // a successful upload with a stale app_token still reports it.
  const ls::UploadResponse response = ls::parseUploadResponse(QStringLiteral(
      "<status>success</status><share>http://prnt.sc/abc</share>"
      "<invalid_token>1</invalid_token>"));
  QVERIFY(response.ok());
  QVERIFY(response.invalidToken);
  QVERIFY(ls::containsInvalidToken(QStringLiteral("<invalid_token")));
  QVERIFY(!ls::containsInvalidToken(QStringLiteral("<tok>invalid_token</tok>")));
}

void TestTagResponse::nonHttpShareIsNotSuccess()
{
  // Share URLs are accepted only with an http:// or https:// prefix, so mailto:/ftp: fails.
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                  "<share>ftp://prnt.sc/abc</share>"))
               .ok());
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                  "<share>mailto:user@example.com</share>"))
               .ok());
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                  "<share></share>"))
               .ok());
  QVERIFY(ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                 "<share>https://prnt.sc/abc</share>"))
              .ok());
}

void TestTagResponse::shareMustStartWithTheScheme()
{
  // Matches Lightshot's client: the scheme must sit at offset 0 and is never trimmed, so a
  // padded share URL is rejected.
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                  "<share> http://prnt.sc/abc</share>"))
               .ok());
  QVERIFY(!ls::parseUploadResponse(QStringLiteral("<status>success</status>"
                                                  "<share>HTTP://prnt.sc/abc</share>"))
               .ok());
  QVERIFY(ls::isHttpUrl(QStringLiteral("http://prnt.sc/abc")));
  QVERIFY(ls::isHttpUrl(QStringLiteral("https://prnt.sc/abc")));
  QVERIFY(!ls::isHttpUrl(QStringLiteral("http:/prnt.sc/abc")));
}

QTEST_APPLESS_MAIN(TestTagResponse)

#include "tst_tagresponse.moc"
