#pragma once

#include "sources.h"

#include <QHash>
#include <QObject>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

// Local listening statistics from the stored history (storage::ListeningStateService;
// docs/api/local-listening-history.md §6). Fails when the runtime keeps no history.
class BackendStats : public QObject, public StatsSource
{
  public:
    explicit BackendStats(runtime::BackendRuntime& runtime, QObject* parent = nullptr);

    Request stats(const StatsQuery& query, Reply<ListeningStats> reply) override;
    Request clear(Reply<bool> reply) override;
    void setChangedHandler(std::function<void()> changed) override;

  private:
    runtime::BackendRuntime& m_runtime;
    QHash<quint64, Reply<ListeningStats>> m_pending;
    std::function<void()> m_changed;
};

} // namespace cirrus::ui
