#pragma once
#include "domain/playback/playback_observation_types.h"
#include <limits>

namespace cirrus::test
{
inline domain::ObservationBinding observationBinding()
{
    return {QUuid("00000000-0000-4000-8000-000000000001"),
            QUuid("00000000-0000-4000-8000-000000000002"),
            QUuid("00000000-0000-4000-8000-000000000003")};
}
inline domain::PlaybackLyricDocument observationDocument()
{
    using namespace domain;
    PlaybackLyricDocument d;
    d.binding = observationBinding();
    d.revision = 2;
    d.lyrics.song = {"9007199254740993"};
    auto& l = d.lyrics;
    l.original.standard.availability = LyricAvailability::Present;
    l.original.standard.timing = LyricTiming::Lines;
    l.original.standard.lines = {{10000, 14000, " 合成 雲 & <text> \" \\ \n😀 ", {}},
                                 {11000, 14000, "duplicate time", {}},
                                 {11000, {}, "last untimed end", {}}};
    l.original.enhanced.availability = LyricAvailability::Present;
    l.original.enhanced.timing = LyricTiming::Words;
    l.original.enhanced.lines = {
        {10000, 14000, "words", {{10000, 1500, "one "}, {11000, 2000, "雲"}}}};
    l.translation.standard.availability = LyricAvailability::Present;
    l.translation.standard.timing = LyricTiming::Lines;
    l.translation.standard.providerOffsetMs = 100;
    l.translation.standard.lines = {{11500, {}, "translation", {}}};
    l.translation.enhanced.availability = LyricAvailability::Empty;
    l.romanization.standard.availability = LyricAvailability::Malformed;
    l.romanization.standard.malformedLines = 5;
    l.romanization.enhanced.availability = LyricAvailability::Present;
    l.romanization.enhanced.lines = {{{}, {}, "plain", {}}};
    l.original.standard.credits = {"synthetic omitted credit"};
    l.providerNoLyric = false;
    l.instrumental = false;
    return d;
}
inline void observationSummaries(domain::PlaybackObservation& p, const domain::SongLyrics& l)
{
    const std::array<const domain::LyricTrack*, 6> ts{
        &l.original.standard,    &l.original.enhanced,     &l.translation.standard,
        &l.translation.enhanced, &l.romanization.standard, &l.romanization.enhanced};
    std::array<domain::ObservationLyricSummary, 6> s;
    for (size_t i = 0; i < 6; ++i)
        s[i] = {ts[i]->availability, ts[i]->timing, ts[i]->providerOffsetMs, ts[i]->lines.size(),
                ts[i]->malformedLines};
    p.lyricTracks = s;
}
inline domain::PlaybackObservation observationValue()
{
    using namespace domain;
    PlaybackObservation p;
    p.binding = observationBinding();
    p.sequence = std::numeric_limits<quint64>::max();
    p.lyricsRevision = 2;
    p.status = ObservationStatus::Ready;
    p.state = PlaybackState::Paused;
    p.intent = TransportIntent::Pause;
    p.song = SongId{"9007199254740993"};
    p.entry = QUuid("00000000-0000-4000-8000-000000000004");
    p.metadataStatus = ObservationContentStatus::Ready;
    p.metadata = PlaybackMetadataView{
        " synthetic 雲 & <text> \" \n", {"same", "same"}, QString(" synthetic album ")};
    p.positionMs = 1250;
    p.durationMs = 30000;
    p.preview = PreviewRange{10000, 40000};
    p.songPositionMs = 11250;
    p.sampleMonoMs = 1500;
    p.positionObservedMonoMs = 1250;
    p.volumeLinear = 0.5f;
    p.mode = ListeningMode::Queue;
    p.order = QueueOrder::Shuffle;
    p.repeat = QueueRepeat::All;
    p.capabilities = {true, true, true, true, true, true, true, true};
    p.lyricsState = LyricsState::Ready;
    p.documentStatus = ObservationContentStatus::Ready;
    p.cursorStatus = ObservationContentStatus::Ready;
    p.userOffsetMs = 25;
    p.providerNoLyric = false;
    p.instrumental = false;
    LyricCursors c;
    c.original.standard.lines = {0, 1, 2};
    c.original.enhanced.lines = {0};
    c.original.enhanced.words = {{0, 0}, {0, 1}};
    p.cursors = c;
    observationSummaries(p, observationDocument().lyrics);
    return p;
}
} // namespace cirrus::test
