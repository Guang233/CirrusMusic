#pragma once

#include "request.h"

#include "domain/music/collection_types.h"

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

// The dispatcher for the facade's library writes (docs/ui/integration.md §2, step 7).
// Writes run one at a time in the order they came, so a like and its undo reach the
// service in that order. A write is for the account signed in when it was asked for:
// never sent once the account changed. Canceling the Request only drops the reply;
// a write asked for is still made (the page showed it done already). A write of
// several steps (batches, rename then description) stops at the first that fails.
class CollectionWrites : public QObject
{
  public:
    // Builds one step's change against the facade as it is when the step is sent;
    // nullopt when it can't be made (fails as Invalid).
    using Step =
        std::function<std::optional<domain::CollectionChange>(application::BackendFacade&)>;
    // The last step's result, when every step was acknowledged.
    using Done = std::function<void(const domain::CollectionResult&)>;
    using Fail = std::function<void(failure::Kind, std::optional<qint64> retryAfterMs)>;

    explicit CollectionWrites(application::BackendFacade& facade, QObject* parent = nullptr);
    ~CollectionWrites() override;

    Request submit(QList<Step> steps, Done done, Fail fail);

    // Maps a write's result (integration.md §2): nullopt when acknowledged. Unknown (sent,
    // effect not seen) is failure::Unknown, which pages tell as "maybe not saved".
    static std::optional<failure::Kind> kindOf(const domain::CollectionResult& result);

  private:
    struct Job {
        quint64 ticket = 0;
        QString accountId; // signed in when asked for
        QList<Step> steps;
        Done done;
        Fail fail;
    };

    [[nodiscard]] bool closed() const;
    void schedulePump();
    void pump();
    void finish(std::optional<failure::Kind> kind, const domain::CollectionResult& result);
    void completed(quint64 id, const domain::CollectionResult& result);

    QPointer<application::BackendFacade> m_facade;
    std::deque<Job> m_waiting;
    std::optional<Job> m_running;
    quint64 m_runningId = 0;            // the facade's id for m_running's current step
    const char* m_runningName = "none"; // that step's change, for the log
    int m_retries = 0;                  // the facade said 0 to this step
    quint64 m_nextTicket = 0;
    QTimer m_retry;
    bool m_pumpScheduled = false;
};

} // namespace cirrus::ui
