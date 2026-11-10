#pragma once

#include "request.h"

#include "domain/music/comment_write_types.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <deque>
#include <functional>
#include <optional>

namespace cirrus::application
{
class BackendFacade;
}

namespace cirrus::ui
{

// The dispatcher for the facade's comment writes (docs/ui/integration.md §2, step 9),
// as CollectionWrites is for the library: one at a time in the order they came (a like
// and its undo reach the service in that order); for the account signed in when asked
// for, never sent once it changed; canceling the Request only drops the reply.
class CommentWrites : public QObject
{
  public:
    using Done = std::function<void()>;
    using Fail = std::function<void(failure::Kind, std::optional<qint64> retryAfterMs)>;

    explicit CommentWrites(application::BackendFacade& facade, QObject* parent = nullptr);
    ~CommentWrites() override;

    Request submit(domain::CommentChange change, Done done, Fail fail);

    // Maps a write's result: nullopt when acknowledged; Unknown (sent, effect not seen)
    // is failure::Unknown, told as "maybe not".
    static std::optional<failure::Kind> kindOf(const domain::CommentWriteResult& result);

  private:
    struct Job {
        quint64 ticket = 0;
        QString accountId; // signed in when asked for
        domain::CommentChange change;
        Done done;
        Fail fail;
    };

    [[nodiscard]] bool closed() const;
    void schedulePump();
    void pump();
    void finish(std::optional<failure::Kind> kind, std::optional<qint64> retryAfterMs = {});
    void completed(quint64 id, const domain::CommentWriteResult& result);

    QPointer<application::BackendFacade> m_facade;
    std::deque<Job> m_waiting;
    std::optional<Job> m_running;
    quint64 m_runningId = 0; // the facade's id for m_running
    int m_retries = 0;       // the facade said 0 to it
    quint64 m_nextTicket = 0;
    QTimer m_retry;
    bool m_pumpScheduled = false;
};

} // namespace cirrus::ui
