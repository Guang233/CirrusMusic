#pragma once
#include "domain/account/session_types.h"
#include "music_types.h"
#include <utility>

namespace cirrus::domain
{
// Local original-source association ONLY: the factory is not a provider-signed receipt.
class SongWikiTextQuery final
{
  public:
    static std::optional<SongWikiTextQuery> fromSong(SessionToken context, const Song& song)
    {
        if (!context.isValid() || !validMusicId(song.id.value) || song.title.isEmpty() ||
            song.title.size() > 4096 || !song.album || !song.album->id ||
            !validMusicId(song.album->id->value))
            return std::nullopt;
        return SongWikiTextQuery(context, song.id, song.title, *song.album->id);
    }
    [[nodiscard]] bool isValid() const
    {
        return m_context.isValid() && validMusicId(m_song.value) && !m_title.isEmpty() &&
               m_title.size() <= 4096 && validMusicId(m_album.value);
    }
    [[nodiscard]] SessionToken sourceContext() const { return m_context; }
    [[nodiscard]] const SongId& song() const { return m_song; }
    [[nodiscard]] const QString& sourceTitle() const { return m_title; }
    [[nodiscard]] const AlbumId& sourceAlbum() const { return m_album; }

  private:
    SongWikiTextQuery(SessionToken context, SongId song, QString title, AlbumId album)
        : m_context(context), m_song(std::move(song)), m_title(std::move(title)),
          m_album(std::move(album))
    {
    }
    SessionToken m_context;
    SongId m_song;
    QString m_title;
    AlbumId m_album;
};
enum class SongWikiIdentity { NotIndependentlyVerified };
enum class SongWikiTextSectionKind { SongInfo, BaseInfo };
struct SongWikiTextItem {
    QString label;
    std::optional<QString> content;
    std::optional<QStringList> texts; // missing != explicit [], no inferred role or joined prose
};
struct SongWikiTextSection {
    SongWikiTextSectionKind kind{};
    QString label;
    std::optional<QString> description;
    QList<SongWikiTextItem> items;
};
struct SongWikiText {
    SongWikiTextQuery query;
    QString observedSongName;
    AlbumId observedAlbum;
    SongWikiIdentity identity = SongWikiIdentity::NotIndependentlyVerified;
    QList<SongWikiTextSection> sections;
    int unsupportedBlockCount = 0;
    bool metadataOmitted =
        true; // by design: URLs/images/actions/IDs/edit/contribution/style/logs omitted
};
inline bool sameSongWikiTextQuery(const SongWikiTextQuery& a, const SongWikiTextQuery& b)
{
    return a.sourceContext() == b.sourceContext() && a.song() == b.song() &&
           a.sourceTitle() == b.sourceTitle() && a.sourceAlbum().value == b.sourceAlbum().value;
}
inline bool matchingSongWikiText(const SongWikiTextQuery& q, const SongWikiText& p)
{
    return sameSongWikiTextQuery(q, p.query) && p.observedSongName == q.sourceTitle() &&
           p.observedAlbum.value == q.sourceAlbum().value &&
           p.identity == SongWikiIdentity::NotIndependentlyVerified;
}
} // namespace cirrus::domain
