#pragma once

#include "domain/account/session_types.h"
#include "domain/music/music_types.h"

#include <QDeadlineTimer>

namespace cirrus::domain
{
enum class AudioQuality { Standard, Exhigh, Lossless, HiRes };
inline QString qualityName(AudioQuality quality)
{
    switch (quality) {
    case AudioQuality::Standard:
        return QStringLiteral("standard");
    case AudioQuality::Exhigh:
        return QStringLiteral("exhigh");
    case AudioQuality::Lossless:
        return QStringLiteral("lossless");
    case AudioQuality::HiRes:
        return QStringLiteral("hires");
    }
    return {};
}
struct StreamQuery {
    SongId song;
    AudioQuality quality = AudioQuality::Standard;
};
struct PreviewRange {
    qint64 startMs = 0;
    qint64 endMs = 0;
};
// Internal application/infrastructure value ONLY: signed URLs never enter UI snapshots/logs.
struct StreamSource {
    SongId song;
    QUrl url;
    QString actualQuality;
    QString format;
    qint64 size = 0;
    QDeadlineTimer validUntil;
    std::optional<PreviewRange> preview;
};
enum class PlaybackError {
    InvalidRequest,
    StaleContext,
    Canceled,
    Unavailable,
    ProtocolKeyMissing,
    Network,
    Timeout,
    RateLimited,
    InvalidResponse,
    UnsafeSource,
    Expired,
    TooLarge,
    Storage,
    UnsupportedFormat,
    Decode,
    NoOutput
};
struct PlaybackFailure {
    PlaybackError error{};
    std::optional<int> providerCode;
};
struct LoadToken {
    SessionToken session;
    quint64 generation = 0;
    friend bool operator==(const LoadToken&, const LoadToken&) = default;
};
enum class PlaybackState { Empty, Resolving, Loading, Ready, Playing, Paused, Ended, Failed };
enum class TransportIntent { Play, Pause, Stop };
struct ObservedPlayback {
    static constexpr qint64 MaximumMs = 7LL * 24 * 60 * 60 * 1000;
    qint64 playedMs = 0;
    bool capped = false;
    friend bool operator==(const ObservedPlayback&, const ObservedPlayback&) = default;
};
struct PlaybackSnapshot {
    LoadToken token;
    SongId song;
    PlaybackState state = PlaybackState::Empty;
    TransportIntent intent = TransportIntent::Stop;
    QString actualQuality;
    std::optional<PreviewRange> preview;
    qint64 positionMs = 0;
    std::optional<qint64> durationMs;
    bool seekable = false;
    std::optional<PlaybackFailure> failure;
    // Monotonic per-load observed progress. Absent means no instrumented backend evidence.
    std::optional<ObservedPlayback> observed;
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::PlaybackSnapshot)
Q_DECLARE_METATYPE(cirrus::domain::LoadToken)
Q_DECLARE_METATYPE(cirrus::domain::PlaybackFailure)
