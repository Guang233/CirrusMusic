#pragma once
#include "domain/music/catalog_types.h"
#include <QUuid>

namespace cirrus::domain
{
// Explicit individual weblog action; no timing policy or automatic two-request sequence.
enum class ListeningReportAction { StartPlay, Play };
struct ListeningReport {
    QUuid eventId; // local correlation only; NOT server idempotency
    SongId song;
    PlaylistId
        playlist; // only pinned list-source payload supported; unknown/FM origin is not fabricated
    ListeningReportAction action = ListeningReportAction::Play;
    qint64 playedSeconds = 0; // caller must supply genuine observed time, never final position
    [[nodiscard]] bool isValid() const
    {
        return !eventId.isNull() && validMusicId(song.value) && validMusicId(playlist.value) &&
               ((action == ListeningReportAction::StartPlay && playedSeconds == 0) ||
                (action == ListeningReportAction::Play && playedSeconds > 0 &&
                 playedSeconds <= 604800));
    }
};
enum class ReportOutcome { NotSent, Acknowledged, Rejected, Unknown };
enum class ReportError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Busy,
    Canceled,
    Network,
    Timeout,
    RateLimited,
    InvalidResponse,
    ProviderRejected,
    ProtocolFailure
};
struct ListeningReportResult {
    QUuid eventId;
    ListeningReportAction action = ListeningReportAction::Play;
    ReportOutcome outcome = ReportOutcome::NotSent;
    ReportError error = ReportError::None;
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs; // information only; never auto retry a write
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::ListeningReportResult)
