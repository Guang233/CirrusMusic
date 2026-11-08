#include "music_requests.h"

#include "application/backend/backend_facade.h"

namespace cirrus::ui
{
namespace
{

// The facade takes 16; a few stay free for playback and the backend's own reads.
constexpr int kRunning = 12;
// When the facade was full anyway, try again after this.
constexpr int kRetryMs = 200;

} // namespace

MusicRequests::MusicRequests(application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_facade(&facade)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &MusicRequests::pump);
    connect(&facade, &application::BackendFacade::stateChanged, this, &MusicRequests::schedulePump);
    connect(&facade, &application::BackendFacade::sessionChanged, this,
            &MusicRequests::schedulePump);
    connect(&facade, &application::BackendFacade::musicCompleted, this,
            [this](quint64 id, domain::SessionToken, const application::MusicResult& result) {
                completed(id, result);
            });
}

MusicRequests::~MusicRequests()
{
    if (m_facade) {
        for (auto it = m_running.cbegin(); it != m_running.cend(); ++it)
            m_facade->cancelMusic(it.key());
    }
}

std::optional<failure::Kind> MusicRequests::kindOf(application::MusicError error)
{
    using application::MusicError;
    switch (error) {
    case MusicError::Canceled:
    case MusicError::StaleContext:
        return std::nullopt;
    case MusicError::Network:
        return failure::Network;
    case MusicError::Timeout:
        return failure::Timeout;
    case MusicError::Busy:
        return failure::Busy;
    case MusicError::RateLimited:
        return failure::RateLimited;
    case MusicError::AuthenticationRequired:
        return failure::SignedOut;
    case MusicError::InvalidRequest:
        return failure::Invalid;
    case MusicError::ProviderRejected:
    case MusicError::InvalidResponse:
    case MusicError::ProtocolFailure:
    case MusicError::IncompleteCatalog:
        return failure::Rejected;
    }
    return failure::Unknown;
}

Request MusicRequests::submit(Send send, Done done, Fail fail)
{
    const quint64 ticket = ++m_nextTicket;
    m_waiting.push_back(Job{ticket, std::move(send), std::move(done), std::move(fail)});
    schedulePump();
    return Request([self = QPointer<MusicRequests>(this), ticket]() {
        if (self)
            self->cancel(ticket);
    });
}

bool MusicRequests::open() const
{
    return m_facade && m_facade->state() == application::BackendState::Ready &&
           m_facade->snapshot().token.isValid();
}

bool MusicRequests::closed() const
{
    if (!m_facade)
        return true;
    const auto state = m_facade->state();
    return state == application::BackendState::Failed ||
           state == application::BackendState::Stopping ||
           state == application::BackendState::Stopped;
}

void MusicRequests::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(this, &MusicRequests::pump, Qt::QueuedConnection);
}

void MusicRequests::pump()
{
    m_pumpScheduled = false;
    if (closed()) {
        auto waiting = std::exchange(m_waiting, {});
        for (Job& job : waiting)
            job.fail(failure::Unknown, std::nullopt);
        return;
    }
    while (!m_waiting.empty() && m_running.size() < kRunning && open()) {
        Job& job = m_waiting.front();
        const quint64 id = job.send(*m_facade);
        if (id == 0) {
            m_retry.start(); // full with others' reads; a moment later
            return;
        }
        if (id == Refused || id == NeedsSignIn) {
            Job refused = std::move(job);
            m_waiting.pop_front();
            refused.fail(id == Refused ? failure::Invalid : failure::SignedOut, std::nullopt);
            continue;
        }
        m_running.insert(id, std::move(job));
        m_waiting.pop_front();
    }
}

void MusicRequests::cancel(quint64 ticket)
{
    for (auto it = m_waiting.begin(); it != m_waiting.end(); ++it) {
        if (it->ticket == ticket) {
            m_waiting.erase(it);
            return;
        }
    }
    for (auto it = m_running.begin(); it != m_running.end(); ++it) {
        if (it->ticket == ticket) {
            if (m_facade)
                m_facade->cancelMusic(it.key());
            m_running.erase(it);
            schedulePump();
            return;
        }
    }
}

void MusicRequests::completed(quint64 id, const application::MusicResult& result)
{
    auto it = m_running.find(id);
    if (it == m_running.end())
        return;
    Job job = std::move(*it);
    m_running.erase(it);
    schedulePump();
    if (const auto* failed = std::get_if<application::MusicFailure>(&result)) {
        if (failed->error == application::MusicError::StaleContext) {
            m_waiting.push_front(std::move(job)); // the session changed: again, on the new one
            return;
        }
        // The service says "too often" (405) as a rejection; pages show it as such.
        if (failed->error == application::MusicError::ProviderRejected &&
            failed->providerCode == 405)
            job.fail(failure::RateLimited, failed->retryAfterMs);
        else if (const auto kind = kindOf(failed->error))
            job.fail(*kind, failed->retryAfterMs);
        return;
    }
    job.done(result);
}

} // namespace cirrus::ui
