#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/playback.h"
#include <QTimer>

namespace cirrus::application
{
// Single-item playback in this slice; queue/FM/recovery/lyrics clock are separate later work.
class PlaybackCoordinator final : public QObject
{
    Q_OBJECT
  public:
    PlaybackCoordinator(SessionCoordinator& session, IStreamResolver& resolver,
                        IPlaybackBackend& backend, QObject* parent = nullptr);
    ~PlaybackCoordinator() override;
    bool start(domain::LoadToken token, domain::StreamQuery query,
               domain::TransportIntent intent = domain::TransportIntent::Play);
    void stop(domain::LoadToken token = {});
    bool pause();
    bool resume();
    bool seek(qint64 positionMs);
    void setVolume(float linear);
    [[nodiscard]] domain::PlaybackSnapshot snapshot() const { return m_snapshot; }
  signals:
    void changed(cirrus::domain::PlaybackSnapshot snapshot);

  private:
    void receive(PlaybackEvent event);
    void fail(domain::PlaybackFailure failure);
    void retire();
    void applyIntent();
    bool current(domain::LoadToken token) const;
    SessionCoordinator& m_session;
    IStreamResolver& m_resolver;
    IPlaybackBackend& m_backend;
    domain::PlaybackSnapshot m_snapshot;
    quint64 m_request = 0;
    quint64 m_lastGeneration = 0; // never reuse a load identity, even after stop/session change
    bool m_ready = false;
    QTimer m_deadline;
    std::optional<qint64> m_seek;
};
} // namespace cirrus::application
