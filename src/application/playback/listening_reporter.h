#pragma once
#include "application/backend/backend_facade.h"
#include "domain/playback/listening_report.h"
#include "domain/playback/listening_state_types.h"
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace cirrus::application
{
class ListeningSession;

// Reports what was listened to (docs/ui/integration.md §6.1, D5): one Play per load
// with the genuinely observed seconds, once it is settled: played to the end, replaced,
// stopped, failed, or paused for longer than the pause timeout. Loads under 3 s are
// not reported, nor guests, nor personal FM (no evidence for its source). The source
// comes from the caller per queue entry; without one the song itself is the source.
// Results are not retried (the facade doesn't either). Caller-thread object; facade and
// session must outlive it.
class ListeningReporter final : public QObject
{
    Q_OBJECT
  public:
    struct Origin {
        domain::ListeningReportSource source = domain::ListeningReportSource::List;
        QString id;
    };
    using OriginLookup = std::function<std::optional<Origin>(QUuid queueEntry)>;
    static constexpr qint64 MinimumMs = 3000;
    static constexpr int DefaultPauseTimeoutMs = 5 * 60 * 1000;

    ListeningReporter(BackendFacade& facade, ListeningSession& session, QObject* parent = nullptr);
    ~ListeningReporter() override;

    void setOriginLookup(OriginLookup lookup) { m_lookup = std::move(lookup); }
    void setPauseTimeout(int ms) { m_pause.setInterval(ms); }
    // Closing: settles the load playing now, then calls `done` once every report in
    // flight has answered, or after `timeoutMs`. `done` runs once, later, never inside.
    void flush(std::function<void()> done, int timeoutMs = 2000);

  private:
    void recordChanged(const domain::StoredListeningRecord& row);
    void playbackChanged(const domain::PlaybackSnapshot& snapshot);
    void report(const domain::StoredListeningRecord& row, domain::ListeningReportEnd end);
    void finishFlush();

    BackendFacade& m_facade;
    ListeningSession& m_session;
    OriginLookup m_lookup;
    std::optional<domain::StoredListeningRecord> m_active; // the load playing, its latest total
    QSet<QUuid> m_reported;                                // record ids already settled
    QList<QUuid> m_reportedOrder;                          // bounds m_reported
    QSet<quint64> m_inFlight;
    QTimer m_pause;
    QTimer m_flushTimer;
    std::function<void()> m_flushDone;
};
} // namespace cirrus::application
