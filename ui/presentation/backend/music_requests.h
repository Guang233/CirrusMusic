#pragma once

#include "request.h"

#include "application/ports/music_gateway.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <deque>
#include <functional>

namespace cirrus::application
{
class BackendFacade;
}

namespace cirrus::ui
{

// The request dispatcher for the facade's music reads (docs/ui/integration.md §2):
// the facade takes 16 at a time and none before it is ready, so reads wait here; a
// read whose session changed under it (StaleContext) is sent again; canceling a
// Request cancels the read. Results arrive on the next event loop turn at the
// earliest, never after cancel.
class MusicRequests : public QObject
{
  public:
    // Sends the read; the facade's command id, 0 when it didn't take it (tried again
    // later: the facade says 0 when full too, so a read it will never take must not
    // say 0, or it holds up all the others), `Refused` when it can't be sent at all
    // (fails as Invalid), `NeedsSignIn` for a signed-in-only read from a guest.
    using Send = std::function<quint64(application::BackendFacade&)>;
    static constexpr quint64 Refused = ~quint64(0);
    static constexpr quint64 NeedsSignIn = ~quint64(0) - 1;
    using Done = std::function<void(const application::MusicResult&)>;
    using Fail = std::function<void(failure::Kind, std::optional<qint64> retryAfterMs)>;

    explicit MusicRequests(application::BackendFacade& facade, QObject* parent = nullptr);
    ~MusicRequests() override;

    // `done` gets every result except a failure, which goes to `fail` already mapped
    // (Canceled never reaches either).
    Request submit(Send send, Done done, Fail fail);

    // Maps a backend failure (integration.md §2); nullopt for Canceled/StaleContext.
    static std::optional<failure::Kind> kindOf(application::MusicError error);

  private:
    struct Job {
        quint64 ticket = 0;
        Send send;
        Done done;
        Fail fail;
    };

    [[nodiscard]] bool open() const;
    [[nodiscard]] bool closed() const;
    void schedulePump();
    void pump();
    void cancel(quint64 ticket);
    void completed(quint64 id, const application::MusicResult& result);

    QPointer<application::BackendFacade> m_facade;
    std::deque<Job> m_waiting;
    QHash<quint64, Job> m_running; // facade command id -> job
    quint64 m_nextTicket = 0;
    QTimer m_retry;
    bool m_pumpScheduled = false;
};

} // namespace cirrus::ui
