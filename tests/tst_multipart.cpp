// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the multipart/form-data body byte layout: a boundary of 27 dashes plus three hex
// fields, all text parts before all file parts, and the per-part header lines.

#include <QtTest>

#include "net/multipart.h"

#include <QFile>
#include <QTemporaryDir>

class TestMultipart : public QObject {
  Q_OBJECT

private slots:
  void boundaryIs27DashesAndThreeHexFields();
  void bodyPlacesAllTextPartsBeforeAllFileParts();
  void filePartWithoutMimeTypeOmitsTheContentTypeLine();
  void filePartFilenameIsTheBasename();
  void unreadableFilePartFails();
};

void TestMultipart::boundaryIs27DashesAndThreeHexFields()
{
  // "---------------------------%04X%04X%04X" is 39 characters: 27 dashes + 3 x 4 hex digits.
  const QByteArray boundary = ls::makeBoundary(0x1a2b, 0x3c4d, 0x5e6f);
  QCOMPARE(boundary.size(), 39);
  QCOMPARE(boundary.left(27), QByteArray(27, '-'));
  QCOMPARE(boundary.count('-'), 27);
  QCOMPARE(boundary.mid(27), QByteArrayLiteral("1A2B3C4D5E6F"));

  // %04X zero-pads and upper-cases; rand() values never exceed 0x7FFF but the format allows more.
  QCOMPARE(ls::makeBoundary(1, 0xabc, 0xffff).mid(27), QByteArrayLiteral("00010ABCFFFF"));

  const QByteArray random = ls::randomBoundary();
  QCOMPARE(random.size(), 39);
  QCOMPARE(random.left(27), QByteArray(27, '-'));
}

void TestMultipart::bodyPlacesAllTextPartsBeforeAllFileParts()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString firstPath = dir.filePath(QStringLiteral("lgt1234.png"));
  const QString secondPath = dir.filePath(QStringLiteral("lgt1234.png_thumb.jpg"));
  {
    QFile first(firstPath);
    QVERIFY(first.open(QIODevice::WriteOnly));
    QVERIFY(first.write(QByteArrayLiteral("PNGDATA")) == 7);
  }
  {
    QFile second(secondPath);
    QVERIFY(second.open(QIODevice::WriteOnly));
    QVERIFY(second.write(QByteArrayLiteral("JPG")) == 3);
  }

  const QByteArray boundary = ls::makeBoundary(0x1a2b, 0x3c4d, 0x5e6f);
  const QList<ls::MultipartTextPart> textParts{
      {QStringLiteral("direct_link"), QStringLiteral("True")},
      {QStringLiteral("width"), QStringLiteral("640")},
  };
  const QList<ls::MultipartFilePart> fileParts{
      {QStringLiteral("image"), firstPath, QStringLiteral("application/octet-stream")},
      {QStringLiteral("thumb"), secondPath, QStringLiteral("application/octet-stream")},
  };

  const std::optional<QByteArray> body = ls::buildMultipartBody(boundary, textParts, fileParts);
  QVERIFY(body.has_value());

  QByteArray expected;
  expected += "--" + boundary
              + "\r\nContent-Disposition: form-data; name=\"direct_link\"\r\n\r\nTrue\r\n";
  expected += "--" + boundary
              + "\r\nContent-Disposition: form-data; name=\"width\"\r\n\r\n640\r\n";
  expected += "--" + boundary
              + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"lgt1234.png\""
                "\r\nContent-Type: application/octet-stream\r\n\r\nPNGDATA\r\n";
  expected += "--" + boundary
              + "\r\nContent-Disposition: form-data; name=\"thumb\"; "
                "filename=\"lgt1234.png_thumb.jpg\""
                "\r\nContent-Type: application/octet-stream\r\n\r\nJPG\r\n";
  expected += "--" + boundary + "--\r\n";
  QCOMPARE(*body, expected);
}

void TestMultipart::filePartWithoutMimeTypeOmitsTheContentTypeLine()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("lgt.png"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QByteArrayLiteral("X")) == 1);
  }

  const QByteArray boundary = QByteArrayLiteral("B");
  const std::optional<QByteArray> body = ls::buildMultipartBody(
      boundary, {}, {{QStringLiteral("image"), path, QString()}});
  QVERIFY(body.has_value());
  // A missing mime type drops the Content-Type line entirely rather than writing it empty.
  QCOMPARE(*body, QByteArrayLiteral("--B\r\nContent-Disposition: form-data; name=\"image\"; "
                                    "filename=\"lgt.png\"\r\n\r\nX\r\n--B--\r\n"));
}

void TestMultipart::filePartFilenameIsTheBasename()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("lgt5678.jpg"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QByteArrayLiteral("J")) == 1);
  }

  const std::optional<QByteArray> body = ls::buildMultipartBody(
      QByteArrayLiteral("B"), {},
      {{QStringLiteral("image"), path, QStringLiteral("application/octet-stream")}});
  QVERIFY(body.has_value());
  // The header carries the basename only; the directory path never reaches the body.
  QVERIFY(body->contains(QByteArrayLiteral("filename=\"lgt5678.jpg\"")));
  QVERIFY(!body->contains(dir.path().toUtf8()));
}

void TestMultipart::unreadableFilePartFails()
{
  const std::optional<QByteArray> body = ls::buildMultipartBody(
      QByteArrayLiteral("B"), {{QStringLiteral("width"), QStringLiteral("1")}},
      {{QStringLiteral("image"), QStringLiteral("/nonexistent/lgt0000.png"),
        QStringLiteral("application/octet-stream")}});
  QVERIFY(!body.has_value());
}

QTEST_APPLESS_MAIN(TestMultipart)

#include "tst_multipart.moc"
