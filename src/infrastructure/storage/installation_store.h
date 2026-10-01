#pragma once

#include "database_worker.h"

#include <functional>

namespace cirrus::storage
{

// Installation-scoped, non-secret state in AppLocalDataLocation/state.sqlite.
// Secrets never go here; they belong to the system keyring (ADR-0006).
class InstallationStore final
{
  public:
    using Random = std::function<std::optional<QByteArray>(qsizetype)>;

    // Absolute path under QStandardPaths::AppLocalDataLocation; empty if the
    // location is unavailable (e.g. application identity not yet configured).
    [[nodiscard]] static QString defaultPath();
    [[nodiscard]] static QList<Migration> migrations();

    // Worker must be opened with migrations(). random defaults to a CSPRNG.
    explicit InstallationStore(DatabaseWorker& worker, Random random = {});

    // Stable device ID: generated once inside a transaction (52 upper-case hex
    // characters, no hardware data), then returned unchanged. An invalid stored
    // value is reported as Corrupt and never silently regenerated.
    void deviceId(QObject* context, std::function<void(StorageResult<QString>)> done);

  private:
    DatabaseWorker& m_worker;
    Random m_random;
};

} // namespace cirrus::storage
