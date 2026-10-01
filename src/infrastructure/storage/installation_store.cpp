#include "installation_store.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QStandardPaths>

namespace cirrus::storage
{
namespace
{

bool validDeviceId(const QString& value)
{
    if (value.isEmpty() || value.size() > 128) {
        return false;
    }
    for (const auto c : value) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

std::optional<QByteArray> systemRandom(qsizetype size)
{
    // QRandomGenerator::system() is the OS CSPRNG (getrandom on Linux).
    QByteArray bytes(size, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()),
                                          static_cast<qsizetype>(size / sizeof(quint32)));
    for (qsizetype i = size - size % qsizetype(sizeof(quint32)); i < size; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::system()->generate());
    }
    return bytes;
}

} // namespace

QString InstallationStore::defaultPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString() : directory + QStringLiteral("/state.sqlite");
}

QList<Migration> InstallationStore::migrations()
{
    return {{1,
             {QStringLiteral("CREATE TABLE installation ("
                             "id INTEGER PRIMARY KEY CHECK (id = 1), "
                             "device_id TEXT NOT NULL CHECK (length(device_id) BETWEEN 1 AND 128), "
                             "created_at INTEGER NOT NULL)")}}};
}

InstallationStore::InstallationStore(DatabaseWorker& worker, Random random)
    : m_worker(worker), m_random(random ? std::move(random) : Random(systemRandom))
{
}

void InstallationStore::deviceId(QObject* context, std::function<void(StorageResult<QString>)> done)
{
    m_worker.run<QString>(
        context,
        [random = m_random](QSqlDatabase& db) -> StorageResult<QString> {
            if (!db.transaction()) {
                return {StorageError::Query, {}};
            }
            const auto fail = [&db](StorageError error) -> StorageResult<QString> {
                db.rollback();
                return {error, {}};
            };
            QSqlQuery select(db);
            if (!select.exec(QStringLiteral("SELECT device_id FROM installation WHERE id = 1"))) {
                return fail(StorageError::Query);
            }
            if (select.next()) {
                const auto stored = select.value(0).toString();
                select.finish();
                if (!validDeviceId(stored)) {
                    return fail(StorageError::Corrupt);
                }
                return db.commit() ? StorageResult<QString>{StorageError::None, stored}
                                   : fail(StorageError::Query);
            }
            select.finish();
            const auto bytes = random(26);
            if (!bytes || bytes->size() != 26) {
                return fail(StorageError::Query);
            }
            const auto generated = QString::fromLatin1(bytes->toHex().toUpper());
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO installation (id, device_id, created_at) VALUES (1, ?, ?)"));
            insert.addBindValue(generated);
            insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
            if (!insert.exec() || !db.commit()) {
                return fail(StorageError::Query);
            }
            return {StorageError::None, generated};
        },
        std::move(done));
}

} // namespace cirrus::storage
