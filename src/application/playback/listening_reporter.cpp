#include "listening_reporter.h"
#include "listening_session.h"
#include "logging/log.h"
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
        if (!m_started.contains(row.id)) { // first heard: the web's startplay
            remember(m_started, m_startedOrder, row.id);
            if (auto start = prepare(row, ListeningReportAction::StartPlay))
                send(*start, row);
        }
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

void ListeningReporter::remember(QSet<QUuid>& set, QList<QUuid>& order, QUuid id)
{
    set.insert(id);
    order.append(id);
    if (order.size() > kRememberedRecords)
        set.remove(order.takeFirst());
}

std::optional<ListeningReporter::Prepared>
ListeningReporter::prepare(const StoredListeningRecord& row, ListeningReportAction action)
{
    const auto session = m_facade.snapshot();
    if (row.source == ListeningMode::PersonalFm || row.accountId.isEmpty() ||
        session.phase != SessionPhase::Authenticated || session.accountId != row.accountId) {
        qCInfo(lcReport) << log::event("report.skip") << log::field("action", action)
                         << log::field("reason", row.source == ListeningMode::PersonalFm ? "fm"
                                                 : row.accountId.isEmpty()               ? "guest"
                                                 : session.phase != SessionPhase::Authenticated
                                                     ? "signed_out"
                                                     : "other_account")
                         << log::field("song", row.song.value);
        return std::nullopt; // FM: no evidence for its source; guests can't; never another
                             // account's
    }
    ListeningReport report{QUuid::createUuid(), row.song, row.song.value, action,
                           action == ListeningReportAction::Play ? row.playedMs / 1000 : 0};
    report.source = ListeningReportSource::Song; // played as itself, unless we know better
    const char* origin = "none";
    if (const auto found = m_lookup ? m_lookup(row.queueEntry) : std::nullopt) {
        auto known = report;
        known.source = found->source;
        known.sourceId = found->id;
        origin = known.isValid() ? "known" : "invalid";
        if (known.isValid())
            report = known;
    }
    return Prepared{report, origin};
}

void ListeningReporter::send(const Prepared& prepared, const StoredListeningRecord& row)
{
    const auto& report = prepared.report;
    qCDebug(lcReport) << log::event("report.settled") << log::field("event", report.eventId)
                      << log::field("action", report.action) << log::field("song", row.song.value)
                      << log::field("record_end", row.end) << log::field("end", report.end)
                      << log::field("played_ms", row.playedMs)
                      << log::field("origin", prepared.origin)
                      << log::field("source", report.source);
    if (const auto id = m_facade.reportListening(report))
        m_inFlight.insert(id);
    else
        qCWarning(lcReport) << log::event("report.not_queued")
                            << log::field("event", report.eventId);
}

void ListeningReporter::report(const StoredListeningRecord& row, ListeningReportEnd end)
{
    if (row.id.isNull() || m_reported.contains(row.id))
        return;
    if (row.playedMs < MinimumMs) {
        qCDebug(lcReport) << log::event("report.skip") << log::field("reason", "short")
                          << log::field("song", row.song.value)
                          << log::field("played_ms", row.playedMs);
        return; // a short load may still grow while it plays
    }
    remember(m_reported, m_reportedOrder, row.id);
    if (auto play = prepare(row, ListeningReportAction::Play)) {
        play->report.end = end;
        send(*play, row);
    }
}

void ListeningReporter::flush(std::function<void()> done, int timeoutMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_flushDone = std::move(done);
    m_session.finishHistoryForStorage(); // settles the playing load (recordChanged)
    qCInfo(lcReport) << log::event("report.flush") << log::field("in_flight", m_inFlight.size());
    if (m_inFlight.isEmpty()) {
        QTimer::singleShot(0, this, &ListeningReporter::finishFlush);
        return;
    }
    m_flushTimer.start(timeoutMs);
}

void ListeningReporter::finishFlush()
{
    if (m_flushDone && !m_inFlight.isEmpty())
        qCWarning(lcReport) << log::event("report.flush_timeout")
                            << log::field("in_flight", m_inFlight.size());
    m_flushTimer.stop();
    if (auto done = std::exchange(m_flushDone, {}))
        done();
}
} // namespace cirrus::application
