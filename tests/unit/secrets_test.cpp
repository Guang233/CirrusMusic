#include "infrastructure/bootstrap/xeapi_key_vault.h"

#include "../support/fake_secret_store.h"

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using namespace cirrus::bootstrap;
using namespace cirrus::netease;
using namespace cirrus::secrets;
using namespace cirrus::test;

class SecretsTest : public QObject
{
    Q_OBJECT
  private slots:
    void restoreValidRecord();
    void restoreFailures_data();
    void restoreFailures();
    void restoreNeverOverwritesNewerKey();
    void changesCoalesceWithoutOverlap();
    void unavailableStoreKeepsMemoryKey();
    void keychainWithoutBackendHasNoPlaintextFallback();
    void keychainRoundTrip();
};

namespace
{
XeapiPublicKey key(const QString& version = QStringLiteral("v7"))
{
    return {version, QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="),
            "sk-test-123"};
}
const QString Entry = QString::fromLatin1(XeapiKeyVault::EntryKey);
} // namespace

void SecretsTest::restoreValidRecord()
{
    FakeSecretStore store;
    store.entries.insert(Entry, XeapiKeyVault::serialize(key()));
    XeapiKeyRing ring;
    XeapiKeyVault vault(ring, store);
    QSignalSpy restored(&vault, &XeapiKeyVault::restored);
    vault.restore();
    QTRY_COMPARE(restored.size(), 1);
    QCOMPARE(qvariant_cast<SecretError>(restored.first().first()), SecretError::None);
    QCOMPARE(ring.current()->version, QStringLiteral("v7"));
    QCOMPARE(ring.current()->sk, key().sk);
    QCOMPARE(store.writes, 0); // restoring is not written back
}

void SecretsTest::restoreFailures_data()
{
    QTest::addColumn<QByteArray>("stored"); // empty = absent
    QTest::addColumn<int>("failWith");
    QTest::addColumn<int>("expected");
    QTest::newRow("absent") << QByteArray() << int(SecretError::None) << int(SecretError::NotFound);
    QTest::newRow("damaged") << QByteArray("{\"format\":1,\"version\":\"v7\"}")
                             << int(SecretError::None) << int(SecretError::Failed);
    QTest::newRow("not-json") << QByteArray("garbage") << int(SecretError::None)
                              << int(SecretError::Failed);
    QTest::newRow("unavailable") << QByteArray() << int(SecretError::Unavailable)
                                 << int(SecretError::Unavailable);
    QTest::newRow("denied") << QByteArray() << int(SecretError::Denied) << int(SecretError::Denied);
}

void SecretsTest::restoreFailures()
{
    QFETCH(QByteArray, stored);
    QFETCH(int, failWith);
    QFETCH(int, expected);
    FakeSecretStore store;
    store.failWith = static_cast<SecretError>(failWith);
    if (!stored.isEmpty()) {
        store.entries.insert(Entry, stored);
    }
    XeapiKeyRing ring;
    XeapiKeyVault vault(ring, store);
    QSignalSpy restored(&vault, &XeapiKeyVault::restored);
    vault.restore();
    QTRY_COMPARE(restored.size(), 1);
    QCOMPARE(int(qvariant_cast<SecretError>(restored.first().first())), expected);
    QVERIFY(!ring.current());
}

void SecretsTest::restoreNeverOverwritesNewerKey()
{
    FakeSecretStore store;
    store.entries.insert(Entry, XeapiKeyVault::serialize(key(QStringLiteral("v6"))));
    store.hold = true;
    XeapiKeyRing ring;
    XeapiKeyVault vault(ring, store);
    QSignalSpy restored(&vault, &XeapiKeyVault::restored);
    vault.restore();
    QVERIFY(ring.replace(key(QStringLiteral("v7")))); // fresh key arrives first
    store.hold = false;
    store.release();
    QTRY_COMPARE(restored.size(), 1);
    QTRY_COMPARE(store.inFlight, 0);
    QCOMPARE(ring.current()->version, QStringLiteral("v7"));
    QCOMPARE(XeapiKeyVault::deserialize(store.entries.value(Entry))->version, QStringLiteral("v7"));
}

void SecretsTest::changesCoalesceWithoutOverlap()
{
    FakeSecretStore store;
    store.hold = true;
    XeapiKeyRing ring;
    XeapiKeyVault vault(ring, store);
    QVERIFY(ring.replace(key(QStringLiteral("v1"))));
    QVERIFY(ring.replace(key(QStringLiteral("v2"))));
    QVERIFY(ring.replace(key(QStringLiteral("v3"))));
    QCOMPARE(store.writes, 1); // later changes wait for the first job
    store.hold = false;
    store.release();
    QTRY_COMPARE(store.writes, 2); // one coalesced write of the latest state
    QTRY_COMPARE(store.inFlight, 0);
    QCOMPARE(store.maxInFlight, 1);
    QCOMPARE(XeapiKeyVault::deserialize(store.entries.value(Entry))->version, QStringLiteral("v3"));

    ring.clear();
    QTRY_COMPARE(store.removes, 1);
    QTRY_VERIFY(!store.entries.contains(Entry));
}

void SecretsTest::unavailableStoreKeepsMemoryKey()
{
    FakeSecretStore store;
    store.failWith = SecretError::Unavailable;
    XeapiKeyRing ring;
    XeapiKeyVault vault(ring, store);
    QSignalSpy failed(&vault, &XeapiKeyVault::persistFailed);
    QVERIFY(ring.replace(key()));
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(qvariant_cast<SecretError>(failed.first().first()), SecretError::Unavailable);
    QCOMPARE(ring.current()->version, QStringLiteral("v7"));
}

void SecretsTest::keychainWithoutBackendHasNoPlaintextFallback()
{
    // No session bus: every secure backend is unreachable. The adapter must fail
    // rather than silently writing QtKeychain's plaintext QSettings fallback.
    QStandardPaths::setTestModeEnabled(true);
    const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir(config).removeRecursively();
    const auto savedBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/cirrus-test-bus");
    KeychainSecretStore store(QStringLiteral("cirrus-test"));
    QObject context;
    std::optional<SecretError> written;
    store.write(QStringLiteral("probe"), "secret-value", &context,
                [&written](SecretError error) { written = error; });
    QTRY_VERIFY_WITH_TIMEOUT(written.has_value(), 10000);
    std::optional<SecretError> invalid;
    store.write(QStringLiteral("Not Valid"), "x", &context,
                [&invalid](SecretError error) { invalid = error; });
    QTRY_VERIFY(invalid.has_value());
    qputenv("DBUS_SESSION_BUS_ADDRESS", savedBus);
    QVERIFY(*written != SecretError::None);
    QCOMPARE(*invalid, SecretError::Failed);
    const auto leftovers = QDir(config).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(u',')));
    QStandardPaths::setTestModeEnabled(false);
}

void SecretsTest::keychainRoundTrip()
{
    // Writes to the user's REAL keyring; opt-in only.
    if (qEnvironmentVariable("CIRRUS_KEYRING_SMOKE") != QStringLiteral("1")) {
        QSKIP("set CIRRUS_KEYRING_SMOKE=1 to exercise the real system keyring");
    }
    KeychainSecretStore store(QStringLiteral("cirrus-keyring-smoke"));
    QObject context;
    std::optional<SecretError> written;
    store.write(QStringLiteral("probe"), "round-trip", &context,
                [&written](SecretError e) { written = e; });
    QTRY_VERIFY_WITH_TIMEOUT(written.has_value(), 30000);
    QCOMPARE(*written, SecretError::None);
    std::optional<SecretResult> read;
    store.read(QStringLiteral("probe"), &context, [&read](SecretResult r) { read = r; });
    QTRY_VERIFY_WITH_TIMEOUT(read.has_value(), 30000);
    QCOMPARE(read->data, QByteArray("round-trip"));
    std::optional<SecretError> removed;
    store.remove(QStringLiteral("probe"), &context, [&removed](SecretError e) { removed = e; });
    QTRY_VERIFY_WITH_TIMEOUT(removed.has_value(), 30000);
    QCOMPARE(*removed, SecretError::None);
    std::optional<SecretResult> gone;
    store.read(QStringLiteral("probe"), &context, [&gone](SecretResult r) { gone = r; });
    QTRY_VERIFY_WITH_TIMEOUT(gone.has_value(), 30000);
    QCOMPARE(gone->error, SecretError::NotFound);
}

QTEST_GUILESS_MAIN(SecretsTest)
#include "secrets_test.moc"
