#pragma once
#include "music_types.h"

namespace cirrus::domain
{
struct ArtistIntroductionQuery {
    ArtistId artist;
    [[nodiscard]] bool isValid() const { return validMusicId(artist.value); }
};
struct IntroductionSection {
    std::optional<QString> title;
    QString text; // untrusted plain text, not HTML or a link instruction
};
struct ArtistIntroduction {
    ArtistId artist;
    std::optional<QString> briefDescription;
    QList<IntroductionSection> sections; // provider order; not merged into ArtistDetails
};
struct SongWikiQuery {
    SongId song;
    [[nodiscard]] bool isValid() const { return validMusicId(song.value); }
};
enum class SongWikiKind { Tags, BusinessTags, Language, Bpm };
struct SongWikiCard {
    SongWikiKind kind{};
    QString title;
    QStringList values; // BPM is provider display text, NOT an inferred numeric tempo
};
struct SongWikiSummary {
    SongId song;
    QList<SongWikiCard> cards;
    bool unsupportedContent = false; // omitted blocks/creative types, NOT completeness evidence
};
} // namespace cirrus::domain
