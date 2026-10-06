#pragma once
#include "domain/account/session_types.h"
#include "playlist_discovery_types.h"
#include <QSet>
#include <utility>

namespace cirrus::domain
{
inline bool validEditablePlaylistTags(const QStringList& tags)
{
    if (tags.isEmpty() || tags.size() > 3) // local budget, NOT a provider maximum
        return false;
    QSet<QString> seen;
    for (const auto& tag : tags) {
        if (!validPlaylistCategory(tag) || tag.contains(u';') || tag == QStringLiteral("全部") ||
            seen.contains(tag))
            return false;
        seen.insert(tag);
    }
    return true;
}
class PlaylistTagEdit final
{
  public:
    static std::optional<PlaylistTagEdit> fromCatalogue(SessionToken context, PlaylistId playlist,
                                                        const PlaylistTags& source,
                                                        QStringList tags)
    {
        if (!context.isValid() || !validMusicId(playlist.value) ||
            source.query.kind != PlaylistTagsKind::Catalogue || source.tags.isEmpty() ||
            source.tags.size() > PlaylistTags::MaximumTags || !validEditablePlaylistTags(tags))
            return std::nullopt;
        QSet<QString> available;
        for (const auto& row : source.tags) {
            if (!validPlaylistCategory(row.name))
                return std::nullopt;
            available.insert(row.name);
        }
        for (const auto& tag : tags)
            if (!available.contains(tag) || (source.all && source.all->name == tag))
                return std::nullopt;
        return PlaylistTagEdit(context, std::move(playlist), std::move(tags));
    }
    [[nodiscard]] bool isValid() const
    {
        return m_context.isValid() && validMusicId(m_playlist.value) &&
               validEditablePlaylistTags(m_tags);
    }
    [[nodiscard]] SessionToken sourceContext() const { return m_context; }
    [[nodiscard]] const PlaylistId& playlist() const { return m_playlist; }
    [[nodiscard]] const QStringList& tags() const { return m_tags; }

  private:
    PlaylistTagEdit(SessionToken context, PlaylistId playlist, QStringList tags)
        : m_context(context), m_playlist(std::move(playlist)), m_tags(std::move(tags))
    {
    }
    SessionToken m_context;
    PlaylistId m_playlist;
    QStringList m_tags; // stripped validated selection, not the full read DTO
};
struct PlaylistMakePublic {
    PlaylistId playlist;
    [[nodiscard]] bool isValid() const { return validMusicId(playlist.value); }
    // One direction only; no caller privacy flag and no owner/visibility inference.
};
} // namespace cirrus::domain
