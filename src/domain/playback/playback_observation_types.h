#pragma once
#include "domain/playback/listening_types.h"
#include <array>

namespace cirrus::domain
{
// Public correlations, NOT account/load tokens, credentials, CAS or signed provenance.
struct ObservationBinding {
    QUuid instanceId, viewEpoch, trackId;
    friend bool operator==(const ObservationBinding&, const ObservationBinding&) = default;
};
enum class ObservationStatus { Unavailable, Ready };
enum class ObservationContentStatus { NotReady, Ready, LimitExceeded, InvalidData };
struct PlaybackMetadataView {
    QString title;
    QStringList artists;
    std::optional<QString> album;
};
struct ObservationLyricSummary {
    LyricAvailability availability{};
    LyricTiming timing{};
    qint64 providerOffsetMs = 0;
    qsizetype lineCount = 0, malformedLines = 0;
};
struct PlaybackObservationCapabilities {
    bool play = false, pause = false, stop = false, next = false, previous = false;
    bool seek = false, volume = false, modes = false;
};
struct PlaybackObservation {
    ObservationBinding binding;
    quint64 sequence = 0, lyricsRevision = 0;
    ObservationStatus status = ObservationStatus::Unavailable;
    PlaybackState state = PlaybackState::Empty;
    TransportIntent intent = TransportIntent::Stop;
    std::optional<SongId> song;
    std::optional<QUuid> entry;
    std::optional<PlaybackMetadataView> metadata;
    ObservationContentStatus metadataStatus{};
    std::optional<qint64> positionMs, durationMs, songPositionMs;
    std::optional<PreviewRange> preview;
    qint64 sampleMonoMs = 0;
    std::optional<qint64> positionObservedMonoMs;
    quint64 positionRevision = 0, seekRevision = 0;
    std::optional<float> volumeLinear;
    ListeningMode mode = ListeningMode::Queue;
    std::optional<QueueOrder> order;
    std::optional<QueueRepeat> repeat;
    bool fmWaiting = false, fmBlocked = false;
    PlaybackObservationCapabilities capabilities;
    std::optional<PlaybackError> failure; // no provider code/text
    LyricsState lyricsState = LyricsState::None;
    ObservationContentStatus documentStatus = ObservationContentStatus::NotReady;
    ObservationContentStatus cursorStatus = ObservationContentStatus::NotReady;
    std::optional<LyricCursors> cursors;
    // Fixed order: original std/enh, translation std/enh, romanization std/enh.
    std::optional<std::array<ObservationLyricSummary, 6>> lyricTracks;
    std::optional<bool> providerNoLyric, instrumental;
    qint64 userOffsetMs = 0;
};
struct PlaybackLyricDocument {
    ObservationBinding binding;
    quint64 revision = 0;
    SongLyrics lyrics; // parsed six tracks; credits removed, never raw provider payload
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::PlaybackObservation)
