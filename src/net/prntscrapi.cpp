// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/prntscrapi.h"

#include "net/httpclient.h"

#include "../app/settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSysInfo>

#include <cmath>

namespace ls {

namespace {

const QString kApiUrl = QStringLiteral("https://api.prntscr.com/v1.1/");

// The account API always sends id 1 and app_type 1.
constexpr int kRequestId = 1;
constexpr int kAppType = 1;

// Random token, about 50 characters. The sign-in page gets it as ?id= and we send it back in
// attach_application; the server doesn't care how it was generated.
QString generateLoginToken()
{
  QString token;
  for (int i = 0; i < 8; ++i)
    token += QString::number(QRandomGenerator::global()->generate(), 16);
  return token.left(50);
}

// JSON body, but the server expects a form-urlencoded Content-Type (Lightshot sends the same).
ls::HttpClient::HeaderList accountHeaders()
{
  return {qMakePair(QByteArrayLiteral("Content-Type"),
                    QByteArrayLiteral("application/x-www-form-urlencoded"))};
}

QByteArray buildBody(const QString& method, const QJsonObject& params)
{
  // JSON object order is not significant, so QJsonObject's sorted order and the compact
  // serialization are fine here.
  QJsonObject request;
  request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  request.insert(QStringLiteral("method"), method);
  request.insert(QStringLiteral("id"), kRequestId);
  request.insert(QStringLiteral("params"), params);
  return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

// The three account keys are always removed together, so "keys present => signed in" stays
// true for the settings store.
void clearAccountState()
{
  ls::Settings& settings = ls::Settings::instance();
  settings.remove(QStringLiteral("username"));
  settings.remove(QStringLiteral("userid"));
  settings.remove(QStringLiteral("token"));
}

struct RpcAnswer {
  bool parsed = false;  // body was JSON and "result" was an object
  bool success = false; // result.success was the boolean true
  QJsonObject result;
};

// All three replies are expected to carry a JSON object with a "result" object inside.
RpcAnswer parseRpcAnswer(const QByteArray& body)
{
  RpcAnswer answer;
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return answer;
  const QJsonValue result = document.object().value(QStringLiteral("result"));
  if (!result.isObject())
    return answer;
  answer.parsed = true;
  answer.result = result.toObject();
  const QJsonValue success = answer.result.value(QStringLiteral("success"));
  answer.success = success.isBool() && success.toBool();
  return answer;
}

// An integral JSON number is accepted as its decimal text, so a numeric "id" in
// get_user's answer does not force a sign-out.
QString stringMember(const QJsonObject& object, const QString& key)
{
  const QJsonValue value = object.value(key);
  if (value.isString())
    return value.toString();
  if (value.isDouble()) {
    const double number = value.toDouble();
    if (std::isfinite(number) && std::floor(number) == number && std::fabs(number) < 1e15)
      return QString::number(static_cast<qlonglong>(number));
  }
  return QString();
}

} // namespace

struct PrntscrApi::Private {
  std::unique_ptr<ls::HttpClient> attach;
  std::unique_ptr<ls::HttpClient> detach;
  std::unique_ptr<ls::HttpClient> user;
  QString loginToken; // pending sign-in token, see beginSignIn()
};

PrntscrApi::PrntscrApi(QObject* parent) : QObject(parent), d(std::make_unique<Private>())
{
  d->attach = std::make_unique<ls::HttpClient>();
  d->detach = std::make_unique<ls::HttpClient>();
  d->user = std::make_unique<ls::HttpClient>();

  connect(d->attach.get(), &ls::HttpClient::finished, this, [this](const ls::HttpResult& result) {
    // Only a parsed reply with success == true and a non-empty "token" signs the user in;
    // every other outcome, including an empty body or a transport error, clears the stored
    // account state.
    const RpcAnswer answer = parseRpcAnswer(result.body);
    const QString token =
        answer.success ? stringMember(answer.result, QStringLiteral("token")) : QString();
    if (answer.parsed && answer.success && !token.isEmpty()) {
      ls::Settings::instance().setToken(token);
      d->loginToken.clear();
      emit attachDone(true);
    } else {
      clearAccountState();
      emit userChanged(QString(), QString());
      emit attachDone(false);
    }
  });

  connect(d->detach.get(), &ls::HttpClient::finished, this, [this](const ls::HttpResult& result) {
    // Success is only reported back to the UI; the stored settings were already cleared when
    // the sign-out started.
    const RpcAnswer answer = parseRpcAnswer(result.body);
    emit detachDone(answer.parsed && answer.success);
  });

  connect(d->user.get(), &ls::HttpClient::finished, this, [this](const ls::HttpResult& result) {
    // An unparsable answer changes nothing; success == false clears the account; success
    // with both fields fills it in.
    const RpcAnswer answer = parseRpcAnswer(result.body);
    if (!answer.parsed) {
      const ls::Settings& settings = ls::Settings::instance();
      emit userChanged(settings.userName(), settings.userId());
      return;
    }
    if (!answer.success) {
      clearAccountState();
      emit userChanged(QString(), QString());
      return;
    }
    const QString name = stringMember(answer.result, QStringLiteral("username"));
    const QString id = stringMember(answer.result, QStringLiteral("id"));
    if (name.isEmpty() || id.isEmpty()) {
      // A successful result missing either field counts as a parse failure.
      const ls::Settings& settings = ls::Settings::instance();
      emit userChanged(settings.userName(), settings.userId());
      return;
    }
    ls::Settings::instance().setUser(name, id);
    emit userChanged(name, id);
  });
}

PrntscrApi::~PrntscrApi() = default;

QString PrntscrApi::beginSignIn()
{
  d->loginToken = generateLoginToken();
  return signInUrl();
}

QString PrntscrApi::signInUrl() const
{
  return QStringLiteral("https://prntscr.com/app/attach_app.php?id=") + d->loginToken;
}

void PrntscrApi::attachApplication()
{
  if (d->attach->busy())
    return;
  if (d->loginToken.isEmpty())
    beginSignIn();

  const ls::Settings& settings = ls::Settings::instance();
  QJsonObject params;
  params.insert(QStringLiteral("app_id"), settings.appId());
  params.insert(QStringLiteral("login_token"), d->loginToken);
  // app_description is the machine name, as Lightshot sends it.
  params.insert(QStringLiteral("app_description"), QSysInfo::machineHostName());
  params.insert(QStringLiteral("app_type"), kAppType);

  d->attach->post(QUrl(kApiUrl), buildBody(QStringLiteral("attach_application"), params),
                  accountHeaders());
}

void PrntscrApi::detachApplication()
{
  if (d->detach->busy())
    return;

  const ls::Settings& settings = ls::Settings::instance();
  // The request body carries the still-stored token, so it is read before the account state
  // is cleared; the clear happens here rather than when the reply arrives.
  const QString appToken = settings.token();
  clearAccountState();
  emit userChanged(QString(), QString());

  QJsonObject params;
  params.insert(QStringLiteral("app_id"), settings.appId());
  params.insert(QStringLiteral("app_token"), appToken);

  d->detach->post(QUrl(kApiUrl), buildBody(QStringLiteral("detach_application"), params),
                  accountHeaders());
}

void PrntscrApi::getUser()
{
  if (d->user->busy())
    return;

  const ls::Settings& settings = ls::Settings::instance();
  QJsonObject params;
  params.insert(QStringLiteral("app_id"), settings.appId());
  params.insert(QStringLiteral("app_token"), settings.token());

  d->user->post(QUrl(kApiUrl), buildBody(QStringLiteral("get_user"), params), accountHeaders());
}

} // namespace ls
