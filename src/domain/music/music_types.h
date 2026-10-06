#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace cirrus::domain
{

// Canonical positive signed-64-bit provider IDs, never floating-point values.
inline bool validMusicId(QStringView value)
{
    if (value.isEmpty() || value.size() > 19 || value.front() < u'1' || value.front() > u'9')
        return false;
    for (auto c : value)
        if (c < u'0' || c > u'9')
            return false;
    bool ok = false;
    const auto number = value.toLongLong(&ok);
    return ok && number > 0;
}

struct SongId {
    QString value;
    friend bool operator==(const SongId&, const SongId&) = default;
};
struct ArtistId {
    QString value;
};
struct AlbumId {
    QString value;
};
struct ArtistCredit {
    std::optional<ArtistId> id;
    QString name;
};
struct AlbumReference {
    std::optional<AlbumId> id;
    QString title;
    QUrl cover;
};
struct Song {
    SongId id;
    QString title;
    QList<ArtistCredit> artists;
    std::optional<AlbumReference> album;
    std::optional<qint64> durationMs;
    std::optional<QStringList> aliases, translatedTitles;
    std::optional<qint64> publishedAtMs;
    // Metadata is NOT proof of playback/download entitlement or URL availability.
};

struct SongSearch {
    QString keywords;
    int limit = 30;
    int offset = 0;
    [[nodiscard]] bool isValid() const
    {
        return !keywords.trimmed().isEmpty() && keywords.size() <= 256 && keywords.isValidUtf16() &&
               !keywords.contains(QChar::Null) && limit >= 1 && limit <= 100 && offset >= 0 &&
               offset <= 10000;
    }
};
struct SongSearchPage {
    SongSearch query;
    QList<Song> songs;
    std::optional<qint64> total;
    std::optional<bool> hasMore; // absent is unknown, never guessed from a full page
};
struct SongDetails {
    QList<Song> songs;     // normalized into requested order
    QList<SongId> missing; // omitted by provider; not automatically a copyright error
};

struct LyricWord {
    qint64 startMs = 0;
    qint64 durationMs = 0;
    QString text;
};
struct LyricLine {
    std::optional<qint64> startMs;
    std::optional<qint64> endMs;
    QString text;
    QList<LyricWord> words;
};
enum class LyricAvailability { Missing, Empty, Present, Malformed };
enum class LyricTiming { PlainText, Lines, Words };
struct LyricTrack {
    LyricAvailability availability = LyricAvailability::Missing;
    LyricTiming timing = LyricTiming::PlainText; // inspect actual format, not a provider field name
    qint64 providerOffsetMs = 0; // positive = display later; do not mutate source timestamps
    QList<LyricLine> lines;      // duplicate/overlapping timestamps retained
    QStringList credits;         // metadata text only, never embedded links/provider JSON
    int malformedLines = 0;
};
struct LyricVersions {
    LyricTrack standard;
    LyricTrack enhanced; // may still be line-timed or absent; never synthesized from standard
};
struct SongLyrics {
    SongId song;
    LyricVersions original;
    LyricVersions translation;
    LyricVersions romanization;
    std::optional<bool> providerNoLyric;
    std::optional<bool> instrumental; // only explicit pureMusic, not an empty lyric array
};

} // namespace cirrus::domain
