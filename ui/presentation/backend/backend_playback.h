#pragma once

#include "sources.h"

#include "domain/playback/playback_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUuid>

namespace cirrus::application
{
class ListeningSession;
}
namespace cirrus::cache
{
class CachedMusicQueryService;
struct CachedMusicResult;
} // namespace cirrus::cache
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// Playback through the backend's ListeningSession (docs/ui/integration.md §4
// PlaybackSource), the only one that sends playback commands. The snapshot is made
// from the session's queue and playback; the current song's details come from
// `catalog`, which must outlive this. Positions only notify when they leave the
// clock's extrapolation.
class BackendPlayback : public QObject, public PlaybackSource
{
  public:
    BackendPlayback(runtime::BackendRuntime& runtime, CatalogSource& catalog,
                    QObject* parent = nullptr);
    ~BackendPlayback() override;

    [[nodiscard]] const PlaybackSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void playSongs(const QStringList& keys, int index) override;
    // The backend doesn't play podcast voices yet (integration.md §4).
    void playVoices(const QStringList&, int) override {}
    [[nodiscard]] bool canPlayVoices() const override { return false; }
    void playPause() override;
    void next() override;
    void previous() override;
    void seek(qint64 positionMs) override;
    void playAt(int index) override;
    void removeAt(int index) override;
    // Not in the backend yet (integration.md §6 D3).
    void move(int, int) override {}
    void setVolume(double volume) override;
    void setOrder(playback::Order order) override;
    void setRepeat(playback::Repeat repeat) override;
    void startFm() override;

  private:
    // Rebuilds the snapshot; notifies on the next turn when it changed.
    void update();
    void notify();
    // The entry id at `index` of the snapshot's queue, null outside it.
    [[nodiscard]] QUuid entryAt(int index) const;
    void fetchDetails(const QString& key);

    QPointer<application::ListeningSession> m_session;
    CatalogSource& m_catalog;
    std::function<void()> m_changed;
    PlaybackSnapshot m_state;
    QList<QUuid> m_entries; // the snapshot's queue as entry ids (Queue mode)
    QStringList m_fmSongs;  // Personal FM: what played so far
    domain::LoadToken m_load;
    quint64 m_positionRevision = 0;
    // Details of songs that played, by key; reset when it grows large.
    QHash<QString, SongInfo> m_details;
    QString m_detailsKey; // asked for
    Request m_detailsRequest;
    bool m_detailsArrived = false;
    std::optional<double> m_volume; // the user's, once set
    bool m_notifyPending = false;
};

// Lyrics: the playing song's from the session, which fetches them anyway (waiting
// while it does); others, or when the session's failed, from the resource cache, then
// the service (integration.md §4 LyricsSource).
class BackendLyrics : public QObject, public LyricsSource
{
  public:
    explicit BackendLyrics(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendLyrics() override;

    Request lyrics(const QString& songKey, Reply<SongLyrics> reply) override;

  private:
    struct Job;
    void sessionLyrics();
    // The cache first, then the service.
    void start(const std::shared_ptr<Job>& job);
    void completed(const cache::CachedMusicResult& result);
    void fromService(const std::shared_ptr<Job>& job);

    QPointer<application::ListeningSession> m_session;
    QPointer<cache::CachedMusicQueryService> m_queries;
    MusicRequests* m_requests;
    QList<std::shared_ptr<Job>> m_waiting;          // for the session's
    QHash<quint64, std::shared_ptr<Job>> m_pending; // by cache query id
};

} // namespace cirrus::ui
