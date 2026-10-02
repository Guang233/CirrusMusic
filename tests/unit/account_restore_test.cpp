#include "../support/fake_http.h"
#include "../support/fake_secret_store.h"
#include "application/account/qr_login_service.h"
#include "infrastructure/netease/auth/account_credential_vault.h"
#include "infrastructure/netease/auth/qr_login_gateway.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::secrets;
using namespace cirrus::test;

namespace
{
const QString Entry = QString::fromLatin1(AccountCredentialVault::EntryKey);
Plan keyPlan() { return {R"({"code":200,"unikey":"synthetic-key"})"}; }
Plan authorization()
{
    Plan p{R"({"code":803})"};
    p.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie",
        "MUSIC_U=synthetic-account; Domain=.music.163.com; Path=/; Secure; HttpOnly"});
    p.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie", "__Host-local=synthetic-local; Path=/; Secure"});
    return p;
}
Plan identity(QByteArray id = "101")
{
    return {"{\"code\":200,\"account\":{\"id\":" + id + "},\"profile\":{\"userId\":" + id +
            ",\"nickname\":\"Synthetic\"}}"};
}

struct Harness {
    SessionCoordinator session;
    FakeSecretStore store;
    FakeManager* http = nullptr;
    std::unique_ptr<SessionApiClient> client;
    std::unique_ptr<AccountCredentialVault> vault;
    std::unique_ptr<QrLoginGateway> gateway;
    std::unique_ptr<QrLoginService> service;

    explicit Harness(QList<Plan> plans = {keyPlan(), authorization(), identity()})
    {
        auto manager = std::make_unique<FakeManager>();
        http = manager.get();
        http->plans = std::move(plans);
        client =
            std::make_unique<SessionApiClient>(std::make_unique<HttpTransport>(std::move(manager)),
                                               QStringLiteral("synthetic-device"));
        vault = std::make_unique<AccountCredentialVault>(*client, store);
        gateway = std::make_unique<QrLoginGateway>(*client, *vault);
        service = std::make_unique<QrLoginService>(session, *gateway);
    }
    bool login(bool remember = true, bool waitStorage = true)
    {
        auto options = service->options();
        options.rememberSession = remember;
        service->setOptions(options);
        QSignalSpy challenge(service.get(), &QrLoginService::challengeReady);
        QSignalSpy signedIn(service.get(), &QrLoginService::signedIn);
        QSignalSpy stored(service.get(), &QrLoginService::savedSessionCompleted);
        const auto attempt = service->begin();
        Q_UNUSED(attempt);
        if (!challenge.wait(2000) || !service->poll() || !signedIn.wait(2000)) {
            return false;
        }
        return !waitStorage || !stored.isEmpty() || stored.wait(2000);
    }
};
} // namespace

class AccountRestoreTest : public QObject
{
    Q_OBJECT
  private slots:
    void rememberRestoreAndLogout();
    void memoryOnly();
    void storeFailureKeepsAuthenticated();
    void failedDeletionSuppressesStoredRecord();
    void mismatchRejected();
    void networkFailureKeepsRecord();
    void logoutOvertakesRead();
    void logoutOvertakesWrite();
    void restartWhileOldReadPending();
    void invalidRecord_data();
    void invalidRecord();
};

void AccountRestoreTest::rememberRestoreAndLogout()
{
    Harness first;
    QVERIFY(first.login());
    QVERIFY(first.store.entries.contains(Entry));
    const auto record = first.store.entries.value(Entry);
    auto profile = identity();
    profile.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie",
        "MUSIC_U=synthetic-rotated; Domain=.music.163.com; Path=/; Secure; HttpOnly"});
    Harness restored({profile, keyPlan()});
    restored.store.entries.insert(Entry, record);
    QSignalSpy ready(restored.service.get(), &QrLoginService::signedIn);
    QSignalSpy stored(restored.service.get(), &QrLoginService::savedSessionCompleted);
    bool boundBeforePublication = false;
    connect(&restored.session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated)
            boundBeforePublication = restored.client->accepts(snapshot.token);
    });
    const auto attempt = restored.service->restore();
    QCOMPARE(restored.session.snapshot().phase, SessionPhase::Authenticating);
    QTRY_COMPARE(ready.size(), 1);
    QVERIFY(boundBeforePublication);
    QCOMPARE(restored.session.snapshot().accountId, QStringLiteral("101"));
    QCOMPARE(restored.session.snapshot().token.epoch, attempt.epoch + 1);
    QCOMPARE(restored.http->requests.size(), 1); // no QR operations during restore
    QCOMPARE(restored.http->requests.first().url().host(), QStringLiteral("music.163.com"));
    QVERIFY(!restored.http->requests.first().rawHeader("Cookie").contains(
        "__Host-local")); // exact-host scope survives
    QTRY_COMPARE(stored.size(), 1);
    QVERIFY(restored.store.entries.value(Entry) != record); // validated response rotated MUSIC_U
    QVERIFY(restored.client->submit(restored.session.snapshot().token, ApiOperation::QrKey) != 0);
    QTRY_COMPARE(restored.http->requests.size(), 2);
    QVERIFY(restored.http->requests.last().rawHeader("Cookie").contains("synthetic-rotated"));
    restored.service->signOut();
    QCOMPARE(restored.session.snapshot().phase, SessionPhase::Guest);
    QTRY_VERIFY(!restored.store.entries.contains(Entry));
    QVERIFY(!restored.client->saveCredentials(restored.session.snapshot().token,
                                              QStringLiteral("101")));
}

void AccountRestoreTest::memoryOnly()
{
    Harness h;
    h.store.entries.insert(Entry, "old-synthetic-record");
    QVERIFY(h.login(false));
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Authenticated);
    QVERIFY(!h.store.entries.contains(Entry));
}

void AccountRestoreTest::storeFailureKeepsAuthenticated()
{
    Harness h;
    h.store.failWith = SecretError::Unavailable;
    QSignalSpy results(h.service.get(), &QrLoginService::savedSessionCompleted);
    QVERIFY(h.login());
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Authenticated);
    QCOMPARE(results.size(), 1);
    QVERIFY(std::holds_alternative<AuthFailure>(
        qvariant_cast<SavedSessionResult>(results.first().at(1))));
    QVERIFY(h.store.entries.isEmpty());
}

void AccountRestoreTest::failedDeletionSuppressesStoredRecord()
{
    Harness first;
    QVERIFY(first.login());
    Harness h({identity()});
    h.store.entries = first.store.entries;
    const auto record = h.store.entries.value(Entry);
    h.store.failWith = SecretError::Failed;
    QSignalSpy storage(h.service.get(), &QrLoginService::savedSessionCompleted);
    h.service->signOut();
    QTRY_COMPARE(storage.size(), 1);
    QVERIFY(std::holds_alternative<AuthFailure>(
        qvariant_cast<SavedSessionResult>(storage.first().at(1))));
    // The fake deletes before reporting failure; model a real backend retaining it.
    h.store.entries.insert(Entry, record);
    h.store.failWith = SecretError::None;
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    const auto attempt = h.service->restore();
    Q_UNUSED(attempt);
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(h.http->requests.isEmpty());
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
}

void AccountRestoreTest::mismatchRejected()
{
    Harness first;
    QVERIFY(first.login());
    Harness h({identity("102")});
    h.store.entries = first.store.entries;
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    const auto attempt = h.service->restore();
    Q_UNUSED(attempt);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(ready.size(), 0);
    QCOMPARE(qvariant_cast<AuthFailure>(failed.first().at(1)).error, AuthError::CredentialRejected);
    QTRY_VERIFY(!h.store.entries.contains(Entry));
    QVERIFY(!h.client->saveCredentials(h.session.snapshot().token, QStringLiteral("102")));
}

void AccountRestoreTest::networkFailureKeepsRecord()
{
    Harness first;
    QVERIFY(first.login());
    Harness h({Plan{"{}", 503}});
    h.store.entries = first.store.entries;
    const auto record = h.store.entries.value(Entry);
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    const auto attempt = h.service->restore();
    Q_UNUSED(attempt);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(h.store.entries.value(Entry), record);
    QCOMPARE(h.store.removes, 0); // network trouble is not proof of revoked credentials
}

void AccountRestoreTest::logoutOvertakesRead()
{
    Harness first;
    QVERIFY(first.login());
    Harness h({identity()});
    h.store.entries = first.store.entries;
    h.store.hold = true;
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    const auto attempt = h.service->restore();
    Q_UNUSED(attempt);
    QTRY_COMPARE(h.store.inFlight, 1);
    h.service->signOut();
    QTest::qWait(1);   // dispatch queued forget while read is still blocked
    h.store.release(); // stale data must not be imported
    h.store.release(); // serialized deletion
    QCOMPARE(ready.size(), 0);
    QVERIFY(h.http->requests.isEmpty());
    QVERIFY(!h.store.entries.contains(Entry));
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
}

void AccountRestoreTest::logoutOvertakesWrite()
{
    Harness h;
    h.store.hold = true;
    QVERIFY(h.login(true, false));
    QTRY_COMPARE(h.store.inFlight, 1);
    h.service->signOut();
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    QTest::qWait(1);
    QCOMPARE(h.store.removes, 0); // no delete overtaking an outstanding write
    h.store.release();
    h.store.release();
    QVERIFY(!h.store.entries.contains(Entry));
    QCOMPARE(h.store.maxInFlight, 1);
}

void AccountRestoreTest::restartWhileOldReadPending()
{
    Harness first;
    QVERIFY(first.login());
    Harness h({identity()});
    h.store.entries = first.store.entries;
    h.store.hold = true;
    const auto old = h.service->restore();
    QTRY_COMPARE(h.store.inFlight, 1);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    const auto next = h.service->restore();
    QVERIFY(next != old);
    h.store.release(); // old context invalid; next read is now dispatched
    h.store.hold = false;
    h.store.release();
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(h.http->requests.size(), 1);
    QCOMPARE(h.session.snapshot().token.epoch, next.epoch + 1);
}

void AccountRestoreTest::invalidRecord_data()
{
    QTest::addColumn<QString>("kind");
    for (const char* row : {"device", "base64", "scope", "expired", "oversize", "format"}) {
        QTest::newRow(row) << QString::fromLatin1(row);
    }
}

void AccountRestoreTest::invalidRecord()
{
    QFETCH(QString, kind);
    Harness first;
    QVERIFY(first.login());
    auto object = QJsonDocument::fromJson(first.store.entries.value(Entry)).object();
    if (kind == QStringLiteral("device"))
        object.insert(QStringLiteral("deviceId"), QStringLiteral("other-device"));
    if (kind == QStringLiteral("format"))
        object.insert(QStringLiteral("format"), 1.5);
    if (kind == QStringLiteral("base64"))
        object.insert(QStringLiteral("cookies"), QStringLiteral("invalid!"));
    if (kind == QStringLiteral("scope") || kind == QStringLiteral("expired")) {
        auto array = QJsonDocument::fromJson(
                         QByteArray::fromBase64(
                             object.value(QStringLiteral("cookies")).toString().toLatin1()))
                         .array();
        for (qsizetype i = 0; i < array.size(); ++i) {
            auto cookie = array.at(i).toObject();
            auto raw =
                QByteArray::fromBase64(cookie.value(QStringLiteral("raw")).toString().toLatin1());
            if (!raw.startsWith("MUSIC_U="))
                continue;
            if (kind == QStringLiteral("scope")) {
                cookie.insert(
                    QStringLiteral("domain"),
                    QStringLiteral("music.163.com")); // raw Domain=.music must not be changed
            } else {
                raw += "; Expires=Thu, 01 Jan 2015 00:00:00 GMT";
                cookie.insert(QStringLiteral("raw"), QString::fromLatin1(raw.toBase64()));
            }
            array.replace(i, cookie);
        }
        object.insert(
            QStringLiteral("cookies"),
            QString::fromLatin1(QJsonDocument(array).toJson(QJsonDocument::Compact).toBase64()));
    }
    const auto record = kind == QStringLiteral("oversize")
                            ? QByteArray(192 * 1024 + 1, 'x')
                            : QJsonDocument(object).toJson(QJsonDocument::Compact);
    Harness h({identity()});
    h.store.entries.insert(Entry, record);
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    const auto attempt = h.service->restore();
    Q_UNUSED(attempt);
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(h.http->requests.isEmpty());
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
}

QTEST_GUILESS_MAIN(AccountRestoreTest)
#include "account_restore_test.moc"
