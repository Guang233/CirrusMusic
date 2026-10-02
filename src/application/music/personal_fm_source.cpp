#include "personal_fm_source.h"
#include <QThread>
#include <algorithm>
#include <utility>

namespace cirrus::application
{
PersonalFmSource::PersonalFmSource(BackendFacade& backend, QObject* parent)
    : QObject(parent), m_backend(backend)
{
    Q_ASSERT(backend.thread() == thread());
    connect(&backend, &BackendFacade::musicCompleted, this, &PersonalFmSource::receive);
    connect(&backend, &BackendFacade::sessionChanged, this, [this] {
        if (m_state.active && !current())
            stop();
    });
    connect(&backend, &BackendFacade::stateChanged, this, [this] {
        if (m_state.active && !current())
            stop();
    });
}
PersonalFmSource::~PersonalFmSource()
{
    disconnect(&m_backend, nullptr, this, nullptr);
    if (m_request)
        m_backend.cancelMusic(m_request);
}
bool PersonalFmSource::authorized() const
{
    return m_backend.state() == BackendState::Ready &&
           m_backend.snapshot().phase == domain::SessionPhase::Authenticated &&
           m_backend.snapshot().token.isValid();
}
bool PersonalFmSource::current() const
{
    return m_state.active && authorized() && m_context == m_backend.snapshot().token;
}
PersonalFmSnapshot PersonalFmSource::snapshot() const
{
    return current() ? m_state : PersonalFmSnapshot{};
}
bool PersonalFmSource::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!authorized() || m_state.active)
        return false;
    ++m_revision;
    m_context = m_backend.snapshot().token;
    m_state = {};
    m_state.active = true;
    m_recent.clear();
    refill();
    return true;
}
void PersonalFmSource::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_revision;
    const auto request = std::exchange(m_request, 0);
    m_state = {};
    m_context = {};
    m_recent.clear();
    if (request)
        m_backend.cancelMusic(request);
    emit changed(snapshot());
}
bool PersonalFmSource::retry()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!current() || m_state.fetching || m_state.upcoming.size() > 2)
        return false;
    m_state.refillBlocked = false;
    m_state.failure.reset();
    refill();
    return true;
}
void PersonalFmSource::refill()
{
    if (!current() || m_state.fetching || m_state.refillBlocked || m_state.upcoming.size() > 2)
        return;
    const auto revision = m_revision;
    m_state.fetching = true; // closes the reentrant submission window before emitting a command
    const auto id = m_backend.personalFmBatch();
    if (revision != m_revision || !current()) {
        if (id)
            m_backend.cancelMusic(id);
        return; // never overwrite a reentrant stop/new source
    }
    m_request = id;
    if (!id) {
        m_state.fetching = false;
        m_state.refillBlocked = true;
        m_state.failure = MusicFailure{MusicError::Busy};
    }
    emit changed(snapshot());
}
std::optional<domain::Song> PersonalFmSource::takeNext()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!current() || m_state.upcoming.isEmpty())
        return std::nullopt;
    const auto revision = m_revision;
    auto song = m_state.upcoming.takeFirst();
    refill();
    if (revision != m_revision || !current())
        return std::nullopt;
    emit changed(snapshot());
    if (revision != m_revision || !current())
        return std::nullopt;
    return song;
}
void PersonalFmSource::receive(quint64 id, domain::SessionToken context, MusicResult result)
{
    if (!m_request || id != m_request || !current() || context != m_context)
        return;
    m_request = 0;
    m_state.fetching = false;
    const auto* batch = std::get_if<domain::PersonalFmBatch>(&result);
    if (!batch || batch->songs.size() > 20 ||
        std::any_of(batch->songs.cbegin(), batch->songs.cend(),
                    [](const auto& s) { return !domain::validMusicId(s.id.value); })) {
        const auto* failure = std::get_if<MusicFailure>(&result);
        m_state.failure = failure ? *failure : MusicFailure{MusicError::InvalidResponse};
        m_state.refillBlocked = true;
    } else {
        qsizetype admitted = 0;
        for (const auto& song : batch->songs) {
            const bool queued = std::any_of(m_state.upcoming.cbegin(), m_state.upcoming.cend(),
                                            [&](const auto& other) { return other.id == song.id; });
            if (queued || m_recent.contains(song.id.value))
                continue;
            m_state.upcoming.append(song);
            m_recent.append(song.id.value);
            if (m_recent.size() > 256)
                m_recent.removeFirst();
            ++admitted;
        }
        m_state.failure.reset();
        m_state.refillBlocked = admitted == 0; // do not spin on empty/duplicate-only batches
    }
    emit changed(snapshot()); // existing upcoming items survive refill failure
}
} // namespace cirrus::application
