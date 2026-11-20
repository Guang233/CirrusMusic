#pragma once

#include "domain/account/session_types.h"
#include "domain/music/music_types.h"
#include "logging/log.h"
#include "media_source.h"
#include <utility>

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
// Song resolver envelope. Identity is checked before the physical base is moved to media.
// Public value inheritance preserves existing song-source field access/initialization;
// the media port accepts ONLY the identity-free base by value, not this envelope.
struct StreamSource : MediaSource {
    SongId song;
    StreamSource() = default;
    StreamSource(SongId id, QUrl location, QString quality, QString encoding, qint64 bytes,
                 QDeadlineTimer deadline, std::optional<PreviewRange> trial = {})
        : MediaSource{std::move(location), std::move(quality), std::move(encoding), bytes, deadline,
                      std::move(trial)},
          song(std::move(id))
    {
    }
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
CIRRUS_LOG_ENUM(PlaybackError, InvalidRequest, StaleContext, Canceled, Unavailable,
                ProtocolKeyMissing, Network, Timeout, RateLimited, InvalidResponse, UnsafeSource,
                Expired, TooLarge, Storage, UnsupportedFormat, Decode, NoOutput)
CIRRUS_LOG_ENUM(AudioQuality, Standard, Exhigh, Lossless, HiRes)
struct PlaybackFailure {
    PlaybackError error{};
    std::optional<int> providerCode;
};
struct LoadToken {
    SessionToken session;
    quint64 generation = 0;
    friend bool operator==(const LoadToken&, const LoadToken&) = default;
};
// How one track goes into the next (docs/tasks/automix.md §3): the engine plans each
// transition (Smart), crossfades for a fixed time (Crossfade), or joins them sample by
// sample (Gapless).
enum class TransitionMode { Smart, Crossfade, Gapless };
CIRRUS_LOG_ENUM(TransitionMode, Smart, Crossfade, Gapless)
enum class PlaybackState { Empty, Resolving, Loading, Ready, Playing, Paused, Ended, Failed };
enum class TransportIntent { Play, Pause, Stop };
CIRRUS_LOG_ENUM(PlaybackState, Empty, Resolving, Loading, Ready, Playing, Paused, Ended, Failed)
CIRRUS_LOG_ENUM(TransportIntent, Play, Pause, Stop)
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
    // Effect evidence, not command acceptance. Per-load counters never reused.
    quint64 positionRevision = 0;
    quint64 seekRevision = 0;
    std::optional<float> volumeLinear; // application output, absent until observed
    std::optional<PlaybackFailure> failure;
    // Monotonic per-load observed progress. Absent means no instrumented backend evidence.
    std::optional<ObservedPlayback> observed;
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::PlaybackSnapshot)
Q_DECLARE_METATYPE(cirrus::domain::LoadToken)
Q_DECLARE_METATYPE(cirrus::domain::PlaybackFailure)
