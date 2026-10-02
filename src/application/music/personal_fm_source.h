#pragma once
#include "application/backend/backend_facade.h"

namespace cirrus::application
{
struct PersonalFmSnapshot {
    bool active = false;
    bool fetching = false;
    bool refillBlocked = false; // failure, empty or duplicate-only batch: explicit retry required
    QList<domain::Song> upcoming;
    std::optional<MusicFailure> failure;
};
// Caller-thread dynamic item supply, NOT a player/ordinary repeating queue.
// One owner consumes takeNext(); do not fetch additional batches directly via facade.
// Facade outlives this object. No timers, automatic retries, feedback or scrobble.
class PersonalFmSource final : public QObject
{
    Q_OBJECT
  public:
    explicit PersonalFmSource(BackendFacade& backend, QObject* parent = nullptr);
    ~PersonalFmSource() override;
    bool start(); // activates the source; an initial Busy failure is observable in snapshot
    void stop();
    bool retry();
    [[nodiscard]] std::optional<domain::Song> takeNext();
    [[nodiscard]] PersonalFmSnapshot snapshot() const;
    [[nodiscard]] bool isBoundTo(const BackendFacade& backend) const
    {
        return &m_backend == &backend;
    }
  signals:
    void changed(cirrus::application::PersonalFmSnapshot snapshot);

  private:
    bool authorized() const;
    bool current() const;
    void refill();
    void receive(quint64 id, domain::SessionToken context, MusicResult result);
    BackendFacade& m_backend;
    domain::SessionToken m_context;
    PersonalFmSnapshot m_state;
    QStringList m_recent; // last 256 admitted IDs; bounded, not a global non-repeat guarantee
    quint64 m_request = 0, m_revision = 0;
};
} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::PersonalFmSnapshot)
