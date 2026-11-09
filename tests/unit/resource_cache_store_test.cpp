// ResourceCacheStore (docs/tasks/resource-cache-backend.md): one connection on the
// cache's own thread; writes and the hits' `touched` go in batches.
#include "infrastructure/cache/resource_cache_codec.h"
#include "infrastructure/cache/resource_cache_store.h"

#include <QDateTime>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace cirrus;
using namespace cirrus::cache;
using namespace cirrus::domain;

namespace
{
QString keyOf(int n)
{
    return coverCacheKey(QUrl(QStringLiteral("https://p1.music.126.net/x/%1.jpg").arg(n)), 256);
}

CacheJob coverJob(quint64 id, CacheOperation operation, int n, QByteArray payload = {})
{
    CacheJob job;
    job.id = id;
    job.operation = operation;
    job.kind = CacheKind::Cover;
    job.key = keyOf(n);
    job.payload = std::move(payload);
    if (operation == CacheOperation::Write) {
        job.ttlMs = CacheDayMs;
        job.notAfterMs = QDateTime::currentMSecsSinceEpoch() + job.ttlMs;
    }
    return job;
}

QByteArray bytesOf(int n) { return QByteArray(1000 + n, char('a' + n % 26)); }
} // namespace

class ResourceCacheStoreTest : public QObject
{
    Q_OBJECT
    // Not under /tmp: the store refuses a root with a group/world-writable ancestor.
    QTemporaryDir m_dir{QDir::currentPath() + QStringLiteral("/cache-store-XXXXXX")};
    std::unique_ptr<storage::DatabaseWorker> m_database;
    quint64 m_next = 0;

    QString root() const { return m_dir.path() + QStringLiteral("/resources"); }

    // Runs `jobs` and waits for each result, by id.
    QHash<quint64, CacheResult> run(ResourceCacheStore& store, const QList<CacheJob>& jobs)
    {
        QSignalSpy done(&store, &ResourceCacheStore::completed);
        for (const auto& job : jobs)
            store.submit(job);
        [&] { QTRY_COMPARE_WITH_TIMEOUT(done.size(), jobs.size(), 10000); }();
        QHash<quint64, CacheResult> results;
        for (const auto& args : done) {
            const auto result = qvariant_cast<CacheResult>(args.at(0));
            results.insert(result.id, result);
        }
        return results;
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(QDir().mkpath(root()));
        QVERIFY(QFile::setPermissions(root(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner));
        m_database = std::make_unique<storage::DatabaseWorker>(
            m_dir.path() + QStringLiteral("/state.sqlite"), QList<storage::Migration>{});
    }

    void missThenWriteThenHit()
    {
        ResourceCacheStore store(*m_database, root());
        const auto miss = run(store, {coverJob(++m_next, CacheOperation::Cover, 1)});
        QCOMPARE(miss.value(m_next).error, CacheError::None);
        QCOMPARE(miss.value(m_next).status, CacheStatus::Miss);

        // Written, and read back before the batch went in.
        const auto write = ++m_next;
        const auto read = ++m_next;
        const auto results = run(store, {coverJob(write, CacheOperation::Write, 1, bytesOf(1)),
                                         coverJob(read, CacheOperation::Cover, 1)});
        QCOMPARE(results.value(write).status, CacheStatus::Stored);
        QCOMPARE(results.value(read).status, CacheStatus::Hit);
        QCOMPARE(std::get<QByteArray>(results.value(read).value), bytesOf(1));
    }

    void batchOfWrites()
    {
        ResourceCacheStore store(*m_database, root());
        QList<CacheJob> writes;
        for (int n = 100; n < 130; ++n) // within CacheJobsInFlight
            writes << coverJob(++m_next, CacheOperation::Write, n, bytesOf(n));
        const auto results = run(store, writes);
        for (const auto& job : writes)
            QCOMPARE(results.value(job.id).status, CacheStatus::Stored);
        CacheJob usage;
        usage.id = ++m_next;
        usage.operation = CacheOperation::Usage;
        usage.kind = CacheKind::Cover;
        const auto counted = run(store, {usage}).value(usage.id);
        QCOMPARE(counted.error, CacheError::None);
        QCOMPARE(counted.usage.entries, 31); // and the one of missThenWriteThenHit
    }

    void keptAcrossInstances()
    {
        ResourceCacheStore store(*m_database, root());
        for (int n : {1, 100, 129}) {
            const auto id = ++m_next;
            const auto hit = run(store, {coverJob(id, CacheOperation::Cover, n)}).value(id);
            QCOMPARE(hit.status, CacheStatus::Hit);
            QCOMPARE(std::get<QByteArray>(hit.value), bytesOf(n));
        }
    }

    void replaceAndClear()
    {
        ResourceCacheStore store(*m_database, root());
        const auto write = ++m_next;
        QCOMPARE(
            run(store, {coverJob(write, CacheOperation::Write, 1, bytesOf(2))}).value(write).status,
            CacheStatus::Stored);
        const auto read = ++m_next;
        QCOMPARE(std::get<QByteArray>(
                     run(store, {coverJob(read, CacheOperation::Cover, 1)}).value(read).value),
                 bytesOf(2));
        CacheJob clear;
        clear.id = ++m_next;
        clear.operation = CacheOperation::ClearAll;
        QCOMPARE(run(store, {clear}).value(clear.id).error, CacheError::None);
        const auto after = ++m_next;
        QCOMPARE(run(store, {coverJob(after, CacheOperation::Cover, 100)}).value(after).status,
                 CacheStatus::Miss);
    }
};

QTEST_GUILESS_MAIN(ResourceCacheStoreTest)
#include "resource_cache_store_test.moc"
