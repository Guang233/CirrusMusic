#include "infrastructure/bootstrap/anonymous_bootstrap.h"

#include "../support/fake_http.h"
#include "../support/fake_secret_store.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::bootstrap;
using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::secrets;
using namespace cirrus::test;

// Synthetic offline data; key material matches the xeapi/netease_crypto vectors.
class BootstrapTest : public QObject
{
    Q_OBJECT
  private slots:
    void tokenExtraction_data();
    void tokenExtraction();
    void freshInstallRegistersAndPersists();
    void restoresWithoutNetwork();
    void otherDeviceTokenIsNotUsed();
    void keyRegistrationFailure();
    void rejectedRegistration_data();
    void rejectedRegistration();
    void keyringUnavailableStillWorksInMemory();
    void anonymousTokenOnlyForGuests();
    void invalidStoredToken_data();
    void invalidStoredToken();
    void resetBeforeDispatch();
    void resetDuringKeyRegistration();
    void resetDuringRestore();
    void resetDuringKeyRestore();
    void resetDuringRegistration();
    void resetDuringPersistence();
    void deletionFailureDoesNotRestoreForgottenToken();
    void staleRestoreRejected();
    void destructionCancelsRegistration();
};

namespace
{
const QString Device = QStringLiteral("synthetic-device");
const QByteArray EncryptedOk =
    QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09");
const QByteArray EncryptedKey =
    "4+hVmeCQwLTgprLInve9fHSNDGu2fQk0eXSJWn0AQwej4hwIPigJPBD8Dppmf+ffzhWn0FTrxRq0zmGbVkri+jEFM3"
    "GCVoc4xtnf2hjsUYggW4Y4rS5k5i1lpQhXxgpJ";
const QByteArray AnonymousCookie = "MUSIC_A=synthetic-anon; Domain=.music.163.com; Path=/";

XeapiPublicKey testKey()
{
    return {QStringLiteral("v7"),
            QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="), "sk-test-123"};
}

Plan keyPlan()
{
    // Registrar nonce is 16 x '1' (random bytes 0x01); see xeapi_test.
    const auto signature = xeapiSignature("1700000000123", "1111111111111111");
    return {R"({"code":200,"data":{"encryptedData":")" + EncryptedKey + R"(","signature":")" +
            signature + R"(","timestamp":"1700000000123"}})"};
}

Plan anonymousPlan(QByteArray body = EncryptedOk, QByteArray cookie = AnonymousCookie)
{
    Plan plan{std::move(body)};
    plan.headers = {{"Set-Cookie", std::move(cookie)}};
    return plan;
}

QByteArray tokenRecord(const QString& device, const QByteArray& token)
{
    return R"({"format":1,"deviceId":")" + device.toLatin1() + R"(","token":")" + token + "\"}";
}

struct Harness {
    FakeManager* api = nullptr;
    FakeManager* keys = nullptr;
    FakeSecretStore store;
    XeapiKeyRing ring;
    SessionToken context{QUuid::createUuid(), 1};
    std::unique_ptr<SessionApiClient> client;
    std::unique_ptr<XeapiKeyRegistrar> registrar;
    std::unique_ptr<XeapiKeyVault> vault;
    std::unique_ptr<AnonymousBootstrap> bootstrap;

    Harness(QList<Plan> apiPlans, QList<Plan> keyPlans)
    {
        auto apiManager = std::make_unique<FakeManager>();
        api = apiManager.get();
        api->plans = std::move(apiPlans);
        auto keyManager = std::make_unique<FakeManager>();
        keys = keyManager.get();
        keys->plans = std::move(keyPlans);
        client = std::make_unique<SessionApiClient>(
            std::make_unique<HttpTransport>(std::move(apiManager)), Device);
        client->setXeapiKeyRing(&ring);
        if (!client->advanceTo(context)) {
            qFatal("Failed to initialize synthetic session context");
        }
        registrar = std::make_unique<XeapiKeyRegistrar>(
            ring, Device, std::make_unique<HttpTransport>(std::move(keyManager)),
            [] { return qint64(1700000000000); },
            [](qsizetype size) { return std::optional<QByteArray>(QByteArray(size, '\x01')); });
        vault = std::make_unique<XeapiKeyVault>(ring, store);
        bootstrap = std::make_unique<AnonymousBootstrap>(*client, ring, *registrar, *vault, store,
                                                         Device, [this] { return context; });
    }

    BootstrapResult run()
    {
        QSignalSpy finished(bootstrap.get(), &AnonymousBootstrap::finished);
        if (!bootstrap->start() || !finished.wait(5000)) {
            return {BootstrapError::Busy};
        }
        return qvariant_cast<BootstrapResult>(finished.first().first());
    }
};
} // namespace

void BootstrapTest::tokenExtraction_data()
{
    QTest::addColumn<QList<QByteArray>>("headers");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("official-domain")
        << QList<QByteArray>{AnonymousCookie} << QByteArray("synthetic-anon");
    QTest::newRow("host-only") << QList<QByteArray>{"MUSIC_A=host-only; Path=/"}
                               << QByteArray("host-only");
    QTest::newRow("absent") << QList<QByteArray>{"NMTID=x; Path=/"} << QByteArray();
    QTest::newRow("wide-domain") << QList<QByteArray>{"MUSIC_A=x; Domain=.163.com; Path=/"}
                                 << QByteArray();
    QTest::newRow("foreign-domain")
        << QList<QByteArray>{"MUSIC_A=x; Domain=.evil.example"} << QByteArray();
    QTest::newRow("expired")
        << QList<QByteArray>{"MUSIC_A=x; Path=/; Expires=Thu, 01 Jan 2015 00:00:00 GMT"}
        << QByteArray();
    QTest::newRow("conflicting") << QList<QByteArray>{"MUSIC_A=a; Path=/", "MUSIC_A=b; Path=/"}
                                 << QByteArray();
    QTest::newRow("repeated-same")
        << QList<QByteArray>{"MUSIC_A=a; Path=/", "MUSIC_A=a; Path=/"} << QByteArray("a");
}

void BootstrapTest::tokenExtraction()
{
    QFETCH(QList<QByteArray>, headers);
    QFETCH(QByteArray, expected);
    const QUrl origin(QStringLiteral("https://interface3.music.163.com/xeapi/register/anonimous"));
    QCOMPARE(anonymousTokenFrom(origin, headers), expected);
}

void BootstrapTest::freshInstallRegistersAndPersists()
{
    Harness h({anonymousPlan(), {R"({"code":200,"unikey":"k"})"}}, {keyPlan()});
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(!result.restored);
    QCOMPARE(h.keys->requests.size(), 1);
    QCOMPARE(h.api->requests.size(), 1);
    QCOMPARE(h.api->requests.first().url().path(), QStringLiteral("/xeapi/register/anonimous"));
    QVERIFY(h.client->hasAnonymousToken());
    QTRY_VERIFY(h.store.entries.contains(QString::fromLatin1(XeapiKeyVault::EntryKey)));
    QCOMPARE(QJsonDocument::fromJson(
                 h.store.entries.value(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)))
                 .object(),
             QJsonDocument::fromJson(tokenRecord(Device, "synthetic-anon")).object());

    // Later guest requests carry the anonymous token.
    QVERIFY(h.client->submit(h.context, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.api->requests.size(), 2);
    QVERIFY(h.api->requests.last().rawHeader("Cookie").contains("MUSIC_A=synthetic-anon"));
}

void BootstrapTest::restoresWithoutNetwork()
{
    Harness h({}, {});
    h.store.entries.insert(QString::fromLatin1(XeapiKeyVault::EntryKey),
                           XeapiKeyVault::serialize(testKey()));
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(Device, "stored-anon"));
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(result.restored);
    QVERIFY(h.client->hasAnonymousToken());
    QVERIFY(h.keys->requests.isEmpty());
    QVERIFY(h.api->requests.isEmpty());
    QCOMPARE(h.store.writes, 0);
}

void BootstrapTest::otherDeviceTokenIsNotUsed()
{
    Harness h({anonymousPlan()}, {});
    QVERIFY(h.ring.replace(testKey()));
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(QStringLiteral("old-device"), "old-anon"));
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(!result.restored);
    QCOMPARE(h.api->requests.size(), 1); // re-registered for this device
    QCOMPARE(QJsonDocument::fromJson(
                 h.store.entries.value(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)))
                 .object(),
             QJsonDocument::fromJson(tokenRecord(Device, "synthetic-anon")).object());
}

void BootstrapTest::keyRegistrationFailure()
{
    Harness h({anonymousPlan()}, {{"{}", 429}});
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::KeyUnavailable);
    QCOMPARE(result.keyError, XeapiKeyError::Transport);
    QVERIFY(h.api->requests.isEmpty()); // never sends xeapi without a key
    QVERIFY(!h.client->hasAnonymousToken());
}

void BootstrapTest::rejectedRegistration_data()
{
    QTest::addColumn<Plan>("plan");
    QTest::addColumn<int>("error");
    QTest::newRow("no-cookie") << anonymousPlan(EncryptedOk, "NMTID=x; Path=/")
                               << int(BootstrapError::Rejected);
    QTest::newRow("bad-cookie-domain") << anonymousPlan(EncryptedOk, "MUSIC_A=x; Domain=.163.com")
                                       << int(BootstrapError::Rejected);
    QTest::newRow("http-error") << Plan{EncryptedOk, 503} << int(BootstrapError::RegisterFailed);
    QTest::newRow("undecryptable")
        << anonymousPlan("not-encrypted") << int(BootstrapError::RegisterFailed);
}

void BootstrapTest::rejectedRegistration()
{
    QFETCH(Plan, plan);
    QFETCH(int, error);
    Harness h({plan}, {});
    QVERIFY(h.ring.replace(testKey()));
    const auto result = h.run();
    QCOMPARE(int(result.error), error);
    QVERIFY(!h.client->hasAnonymousToken());
    QVERIFY(!h.store.entries.contains(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)));
}

void BootstrapTest::keyringUnavailableStillWorksInMemory()
{
    Harness h({anonymousPlan()}, {keyPlan()});
    h.store.failWith = SecretError::Unavailable;
    QSignalSpy persistFailed(h.bootstrap.get(), &AnonymousBootstrap::persistFailed);
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(h.client->hasAnonymousToken());
    QVERIFY(h.ring.current());
    QCOMPARE(persistFailed.size(), 1);
    QVERIFY(h.store.entries.isEmpty());
}

void BootstrapTest::anonymousTokenOnlyForGuests()
{
    Plan authorized{R"({"code":803})"};
    authorized.headers = {{"Set-Cookie", "MUSIC_U=synthetic-user; Domain=.music.163.com; Path=/"}};
    Harness h({authorized, {R"({"code":200,"unikey":"k"})"}}, {});
    h.client->setAnonymousToken("synthetic-anon");
    QSignalSpy spy(h.client.get(), &SessionApiClient::completed);
    QVERIFY(h.client->submit(h.context, ApiOperation::QrCheck,
                             {{QStringLiteral("key"), QStringLiteral("k")}}) > 0);
    QTRY_COMPARE(spy.size(), 1);
    QVERIFY(h.api->requests.first().rawHeader("Cookie").contains("MUSIC_A=synthetic-anon"));
    QVERIFY(h.client->submit(h.context, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 2);
    const auto cookie = h.api->requests.last().rawHeader("Cookie");
    QVERIFY(cookie.contains("MUSIC_U=synthetic-user"));
    QVERIFY(!cookie.contains("synthetic-anon")); // logged-in requests never add it

    // The installation token survives an epoch change (unlike account cookies).
    const SessionToken next{h.context.coordinatorId, h.context.epoch + 1};
    QVERIFY(h.client->advanceTo(next));
    QVERIFY(h.client->hasAnonymousToken());
}

void BootstrapTest::invalidStoredToken_data()
{
    QTest::addColumn<QByteArray>("record");
    QTest::newRow("lossy-unicode")
        << QByteArray(R"({"format":1,"deviceId":"synthetic-device","token":"\u4e2d"})");
    QTest::newRow("fractional-format")
        << QByteArray(R"({"format":1.5,"deviceId":"synthetic-device","token":"old"})");
    QTest::newRow("oversize") << QByteArray(8193, 'x');
}

void BootstrapTest::invalidStoredToken()
{
    QFETCH(QByteArray, record);
    Harness h({anonymousPlan()}, {});
    QVERIFY(h.ring.replace(testKey()));
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey), record);
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(!result.restored);
    QCOMPARE(h.api->requests.size(), 1);
}

void BootstrapTest::resetBeforeDispatch()
{
    Harness h({}, {});
    QVERIFY(h.ring.replace(testKey()));
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    h.bootstrap->reset(); // before the queued key-ready callback
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<BootstrapResult>(finished.first().first()).error,
             BootstrapError::Canceled);
    QVERIFY(h.api->requests.isEmpty());
    QVERIFY(!h.client->hasAnonymousToken());
}

void BootstrapTest::resetDuringKeyRegistration()
{
    auto plan = keyPlan();
    plan.delayMs = 200;
    Harness h({}, {plan});
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.keys->requests.size(), 1);
    h.bootstrap->reset();
    QVERIFY(!h.bootstrap->start());
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<BootstrapResult>(finished.first().first()).error,
             BootstrapError::Canceled);
    QVERIFY(h.ring.current());          // installation-wide key work may finish normally
    QVERIFY(h.api->requests.isEmpty()); // no anonymous continuation
    QVERIFY(!h.client->hasAnonymousToken());
}

void BootstrapTest::resetDuringRestore()
{
    Harness h({}, {});
    QVERIFY(h.ring.replace(testKey()));
    QTRY_COMPARE(h.store.inFlight, 0); // shared key vault has settled
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(Device, "old-anon"));
    h.store.hold = true;
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.store.inFlight, 1);
    h.bootstrap->reset();
    QVERIFY(!h.bootstrap->start());
    h.store.release(); // old read must not adopt the token
    QVERIFY(!h.client->hasAnonymousToken());
    QCOMPARE(h.store.removes, 1);
    h.store.release(); // erase acknowledges reset
    QCOMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<BootstrapResult>(finished.first().first()).error,
             BootstrapError::Canceled);
    QVERIFY(h.api->requests.isEmpty());
    QVERIFY(!h.store.entries.contains(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)));
}

void BootstrapTest::resetDuringKeyRestore()
{
    Harness h({}, {});
    h.store.hold = true;
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    h.bootstrap->reset();
    h.bootstrap->reset(); // coalesced, no concurrent remove
    QVERIFY(!h.bootstrap->start());
    h.store.release();
    QCOMPARE(h.store.removes, 1);
    h.store.release();
    QCOMPARE(finished.size(), 1);
    QVERIFY(h.keys->requests.isEmpty());
    QVERIFY(h.api->requests.isEmpty());
}

void BootstrapTest::resetDuringRegistration()
{
    auto plan = anonymousPlan();
    plan.delayMs = -1;
    Harness h({plan}, {});
    QVERIFY(h.ring.replace(testKey()));
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.api->requests.size(), 1);
    h.bootstrap->reset();
    QVERIFY(!h.bootstrap->start());
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<BootstrapResult>(finished.first().first()).error,
             BootstrapError::Canceled);
    QVERIFY(!h.client->hasAnonymousToken());
    QVERIFY(!h.store.entries.contains(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)));
}

void BootstrapTest::resetDuringPersistence()
{
    auto plan = anonymousPlan();
    plan.delayMs = 200;
    Harness h({plan, anonymousPlan()}, {});
    QVERIFY(h.ring.replace(testKey()));
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.api->requests.size(), 1);
    QTRY_COMPARE(h.store.inFlight, 0);
    h.store.hold = true;
    QTRY_COMPARE(h.store.inFlight, 1); // write has reached the keyring
    QVERIFY(h.client->hasAnonymousToken());
    h.bootstrap->reset();
    QVERIFY(!h.client->hasAnonymousToken());
    QCOMPARE(h.store.removes, 0); // must not delete before an outstanding write
    QVERIFY(!h.bootstrap->start());
    h.store.release(); // late write completes; now deletion starts
    QCOMPARE(h.store.removes, 1);
    h.store.release();
    QCOMPARE(finished.size(), 1);
    QVERIFY(!h.store.entries.contains(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey)));
    QVERIFY(!h.client->hasAnonymousToken());
    h.store.hold = false;
    const auto result = h.run(); // immediate restart is safe after reset acknowledgement
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(!result.restored);
    QCOMPARE(h.api->requests.size(), 2);
}

void BootstrapTest::deletionFailureDoesNotRestoreForgottenToken()
{
    Harness h({anonymousPlan()}, {});
    QVERIFY(h.ring.replace(testKey()));
    QTRY_COMPARE(h.store.inFlight, 0);
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(Device, "old-anon"));
    h.store.failWith = SecretError::Denied;
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QSignalSpy failure(h.bootstrap.get(), &AnonymousBootstrap::persistFailed);
    h.bootstrap->reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(failure.size(), 1);
    // Model failed backend deletion retaining the old record.
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(Device, "old-anon"));
    h.store.failWith = SecretError::None;
    const auto result = h.run();
    QCOMPARE(result.error, BootstrapError::None);
    QVERIFY(!result.restored);
    QCOMPARE(h.api->requests.size(), 1);
}

void BootstrapTest::staleRestoreRejected()
{
    Harness h({}, {});
    QVERIFY(h.ring.replace(testKey()));
    QTRY_COMPARE(h.store.inFlight, 0);
    h.store.entries.insert(QString::fromLatin1(AnonymousBootstrap::TokenEntryKey),
                           tokenRecord(Device, "old-anon"));
    h.store.hold = true;
    QSignalSpy finished(h.bootstrap.get(), &AnonymousBootstrap::finished);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.store.inFlight, 1);
    const SessionToken next{h.context.coordinatorId, h.context.epoch + 1};
    h.context = next;
    QVERIFY(h.client->advanceTo(next));
    h.store.release();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<BootstrapResult>(finished.first().first()).apiError,
             ApiError::StaleContext);
    QVERIFY(!h.client->hasAnonymousToken());
}

void BootstrapTest::destructionCancelsRegistration()
{
    auto plan = anonymousPlan();
    plan.delayMs = -1;
    Harness h({plan}, {});
    QVERIFY(h.ring.replace(testKey()));
    QSignalSpy completed(h.client.get(), &SessionApiClient::completed);
    QVERIFY(h.bootstrap->start());
    QTRY_COMPARE(h.api->requests.size(), 1);
    h.bootstrap.reset();
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(qvariant_cast<ApiResult>(completed.first().at(2)).error, ApiError::Canceled);
    QVERIFY(!h.client->hasAnonymousToken());
}

QTEST_GUILESS_MAIN(BootstrapTest)
#include "bootstrap_test.moc"
