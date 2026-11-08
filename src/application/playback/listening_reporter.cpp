#include "listening_reporter.h"
#include "listening_session.h"
#include <QThread>

namespace cirrus::application
{
using namespace domain;
namespace
{
constexpr qsizetype kRememberedRecords = 512;

ListeningReportEnd endOf(ListeningEnd end)
{
    switch (end) {
    case ListeningEnd::Ended:
        return ListeningReportEnd::PlayEnd;
    case ListeningEnd::Replaced:
        return ListeningReportEnd::Ui; // the user went on (an automatic advance ends as Ended)
    case ListeningEnd::Failed:
        return ListeningReportEnd::Exception;
    default:
        return ListeningReportEnd::Interrupt;
    }
}
} // namespace

ListeningReporter::ListeningReporter(BackendFacade& facade, ListeningSession& session,
                                     QObject* parent)
    : QObject(parent), m_facade(facade), m_session(session)
{
    Q_ASSERT(facade.thread() == thread() && session.thread() == thread());
    m_pause.setSingleShot(true);
    m_pause.setInterval(DefaultPauseTimeoutMs);
    connect(&m_pause, &QTimer::timeout, this, [this] {
        if (m_active && m_session.playback().state == PlaybackState::Paused)
            report(*m_active, ListeningReportEnd::Interrupt);
    });
    m_flushTimer.setSingleShot(true);
    connect(&m_flushTimer, &QTimer::timeout, this, &ListeningReporter::finishFlush);
    connect(&session, &ListeningSession::listeningRecordChanged, this,
            &ListeningReporter::recordChanged);
    connect(&session, &ListeningSession::playbackChanged, this,
            &ListeningReporter::playbackChanged);
    connect(&facade, &BackendFacade::listeningReportCompleted, this, [this](quint64 id) {
        if (m_inFlight.remove(id) && m_inFlight.isEmpty() && m_flushDone)
            finishFlush();
    });
}

ListeningReporter::~ListeningReporter() = default;

void ListeningReporter::recordChanged(const StoredListeningRecord& row)
{
    if (row.end == ListeningEnd::Active) {
        m_active = row;
        return;
    }
    if (m_active && m_active->id == row.id)
        m_active.reset();
    report(row, endOf(row.end));
}

void ListeningReporter::playbackChanged(const PlaybackSnapshot& snapshot)
{
    if (snapshot.state == PlaybackState::Paused) {
        if (!m_pause.isActive())
            m_pause.start();
    } else
        m_pause.stop();
}

void ListeningReporter::report(const StoredListeningRecord& row, ListeningReportEnd end)
{
    if (row.id.isNull() || m_reported.contains(row.id) || row.playedMs < MinimumMs)
        return; // a short load may still grow while it plays
    m_reported.insert(row.id);
    m_reportedOrder.append(row.id);
    if (m_reportedOrder.size() > kRememberedRecords)
        m_reported.remove(m_reportedOrder.takeFirst());
    const auto session = m_facade.snapshot();
    if (row.source == ListeningMode::PersonalFm || row.accountId.isEmpty() ||
        session.phase != SessionPhase::Authenticated || session.accountId != row.accountId)
        return; // FM: no evidence for its source; guests can't; never another account's
    ListeningReport report{QUuid::createUuid(), row.song, row.song.value,
                           ListeningReportAction::Play, row.playedMs / 1000};
    report.source = ListeningReportSource::Song; // played as itself, unless we know better
    report.end = end;
    if (const auto origin = m_lookup ? m_lookup(row.queueEntry) : std::nullopt) {
        auto known = report;
        known.source = origin->source;
        known.sourceId = origin->id;
        if (known.isValid())
            report = known;
    }
    if (const auto id = m_facade.reportListening(report))
        m_inFlight.insert(id);
}

void ListeningReporter::flush(std::function<void()> done, int timeoutMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_flushDone = std::move(done);
    m_session.finishHistoryForStorage(); // settles the playing load (recordChanged)
    if (m_inFlight.isEmpty()) {
        QTimer::singleShot(0, this, &ListeningReporter::finishFlush);
        return;
    }
    m_flushTimer.start(timeoutMs);
}

void ListeningReporter::finishFlush()
{
    m_flushTimer.stop();
    if (auto done = std::exchange(m_flushDone, {}))
        done();
}
} // namespace cirrus::application
