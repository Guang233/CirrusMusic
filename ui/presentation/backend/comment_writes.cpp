#include "comment_writes.h"

#include "application/backend/backend_facade.h"
#include "logging/log.h"

#include <QUuid>

#include <type_traits>

namespace cirrus::ui
{
namespace
{

// The facade said 0 although ready, signed in and the change valid: full. A moment
// later, a few times.
constexpr int kRetryMs = 200;
constexpr int kRetries = 5;

const char* changeName(const domain::CommentChange& change)
{
    return std::visit(
        [](const auto& c) -> const char* {
            using C = std::remove_cvref_t<decltype(c)>;
            if constexpr (std::is_same_v<C, domain::CommentReaction>)
                return c.liked ? "like" : "unlike";
            else if constexpr (std::is_same_v<C, domain::CommentPublish>)
                return "publish";
            else if constexpr (std::is_same_v<C, domain::CommentReply>)
                return "reply";
            else
                return "delete";
        },
        change);
}

} // namespace

CommentWrites::CommentWrites(application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_facade(&facade)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &CommentWrites::pump);
    connect(&facade, &application::BackendFacade::stateChanged, this, &CommentWrites::schedulePump);
    connect(&facade, &application::BackendFacade::sessionChanged, this,
            &CommentWrites::schedulePump);
    connect(&facade, &application::BackendFacade::commentWriteCompleted, this,
            [this](quint64 id, domain::SessionToken, const domain::CommentWriteResult& result) {
                completed(id, result);
            });
}

// Writes still running at exit are left to the facade (it settles them).
CommentWrites::~CommentWrites() = default;

std::optional<failure::Kind> CommentWrites::kindOf(const domain::CommentWriteResult& result)
{
    using domain::CommentWriteError;
    using domain::CommentWriteOutcome;
    if (result.outcome == CommentWriteOutcome::Acknowledged)
        return std::nullopt;
    if (result.outcome == CommentWriteOutcome::Unknown)
        return failure::Unknown; // sent; whether it took is not known
    switch (result.error) {
    case CommentWriteError::Network:
        return failure::Network;
    case CommentWriteError::Timeout:
        return failure::Timeout;
    case CommentWriteError::Busy:
        return failure::Busy;
    case CommentWriteError::RateLimited:
        return failure::RateLimited;
    case CommentWriteError::AuthenticationRequired:
    case CommentWriteError::StaleContext: // not sent: the account changed
        return failure::SignedOut;
    case CommentWriteError::InvalidRequest:
        return failure::Invalid;
    case CommentWriteError::ProviderRejected:
        // The service says "too often" (405) as a rejection.
        return result.providerCode == 405 ? failure::RateLimited : failure::Rejected;
    case CommentWriteError::InvalidResponse:
    case CommentWriteError::ProtocolFailure:
    case CommentWriteError::TokenUnavailable: // the publish token could not be had: not sent
        return failure::Rejected;
    case CommentWriteError::None:
    case CommentWriteError::Canceled:
        break;
    }
    return failure::Unknown;
}

Request CommentWrites::submit(domain::CommentChange change, Done done, Fail fail)
{
    const quint64 ticket = ++m_nextTicket;
    QString account;
    if (m_facade && m_facade->snapshot().phase == domain::SessionPhase::Authenticated)
        account = m_facade->snapshot().accountId;
    m_waiting.push_back(Job{ticket, account, std::move(change), std::move(done), std::move(fail)});
    schedulePump();
    return Request([self = QPointer<CommentWrites>(this), ticket]() {
        if (!self)
            return;
        // Only the reply goes; the write is still made.
        auto drop = [](Job& job) {
            job.done = {};
            job.fail = {};
        };
        if (self->m_running && self->m_running->ticket == ticket)
            drop(*self->m_running);
        for (Job& job : self->m_waiting)
            if (job.ticket == ticket)
                drop(job);
    });
}

bool CommentWrites::closed() const
{
    if (!m_facade)
        return true;
    const auto state = m_facade->state();
    return state == application::BackendState::Failed ||
           state == application::BackendState::Stopping ||
           state == application::BackendState::Stopped;
}

void CommentWrites::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(this, &CommentWrites::pump, Qt::QueuedConnection);
}

void CommentWrites::finish(std::optional<failure::Kind> kind, std::optional<qint64> retryAfterMs)
{
    Job job = std::move(*m_running);
    m_running.reset();
    m_runningId = 0;
    schedulePump();
    if (!kind) {
        if (job.done)
            job.done();
    } else if (job.fail) {
        job.fail(*kind, retryAfterMs);
    }
}

void CommentWrites::pump()
{
    m_pumpScheduled = false;
    if (closed()) {
        if (m_running && m_runningId == 0)
            finish(failure::Unknown);
        auto waiting = std::exchange(m_waiting, {});
        for (Job& job : waiting)
            if (job.fail)
                job.fail(failure::Unknown, std::nullopt);
        return;
    }
    if (!m_running) {
        if (m_waiting.empty())
            return;
        m_running = std::move(m_waiting.front());
        m_waiting.pop_front();
        m_retries = 0;
    }
    if (m_runningId != 0)
        return; // with the facade
    if (m_facade->state() != application::BackendState::Ready)
        return; // starting: once ready
    const auto session = m_facade->snapshot();
    if (m_running->accountId.isEmpty() || session.phase != domain::SessionPhase::Authenticated ||
        session.accountId != m_running->accountId) {
        qCInfo(lcUiBackend) << log::event("comment_write.dropped")
                            << log::field("reason", "account");
        finish(failure::SignedOut);
        return;
    }
    const domain::CommentMutation mutation{QUuid::createUuid(), m_running->change};
    if (!mutation.isValid()) {
        qCWarning(lcUiBackend) << log::event("comment_write.invalid")
                               << log::field("change", changeName(m_running->change));
        finish(failure::Invalid);
        return;
    }
    const auto id = m_facade->mutateComment(mutation);
    if (id == 0) {
        if (++m_retries <= kRetries) {
            m_retry.start();
            return;
        }
        qCWarning(lcUiBackend) << log::event("comment_write.refused")
                               << log::field("change", changeName(m_running->change));
        finish(failure::Busy);
        return;
    }
    m_runningId = id;
}

void CommentWrites::completed(quint64 id, const domain::CommentWriteResult& result)
{
    if (!m_running || id != m_runningId)
        return;
    m_runningId = 0;
    const char* name = changeName(m_running->change);
    const auto kind = kindOf(result);
    if (kind)
        qCWarning(lcUiBackend) << log::event("comment_write.failed") << log::field("change", name)
                               << log::field("outcome", result.outcome)
                               << log::field("err", result.error)
                               << log::field("code", result.providerCode.value_or(0));
    else
        qCInfo(lcUiBackend) << log::event("comment_write.done") << log::field("change", name);
    finish(kind, result.retryAfterMs);
}

} // namespace cirrus::ui
