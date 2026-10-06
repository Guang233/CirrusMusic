#pragma once
#include "entity_search_types.h"

namespace cirrus::domain
{
// Do not convert a search VoiceId into a legacy program id without evidence.
struct ProgramId {
    QString value;
};
struct PodcastDetailQuery {
    PodcastId podcast;
    [[nodiscard]] bool isValid() const { return validMusicId(podcast.value); }
};
enum class ProgramOrder { NewestFirst, OldestFirst };
struct PodcastProgramsQuery {
    PodcastId podcast;
    CatalogPage page;
    ProgramOrder order = ProgramOrder::NewestFirst;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(podcast.value) && page.isValid() &&
               (order == ProgramOrder::NewestFirst || order == ProgramOrder::OldestFirst);
    }
};
struct ProgramDetailQuery {
    ProgramId program;
    [[nodiscard]] bool isValid() const { return validMusicId(program.value); }
};
struct Program {
    ProgramId id;
    QString title;
    QUrl cover;
    std::optional<QString> description;
    std::optional<Podcast> podcast;
    std::optional<qint64> providerDuration, serialNumber;
    // No mainSong.id, URL, SongId, rights or playback/reporting adapter.
};
struct PodcastDetails {
    PodcastDetailQuery query;
    Podcast podcast;
};
struct PodcastProgramsPage {
    PodcastProgramsQuery query;
    QList<Program> programs;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct ProgramDetails {
    ProgramDetailQuery query;
    Program program;
};
} // namespace cirrus::domain
