#pragma once
#include "domain/account/session_types.h"
#include "library_types.h"
#include <QSet>
#include <utility>

namespace cirrus::domain
{
struct PlaylistDelete {
    PlaylistId playlist;
    [[nodiscard]] bool isValid() const { return validMusicId(playlist.value); }
};
namespace playlist_order_detail
{
template <class Id> bool uniqueIds(const QList<Id>& ids, qsizetype maximum)
{
    if (ids.isEmpty() || ids.size() > maximum)
        return false;
    QSet<QString> seen;
    for (const auto& id : ids) {
        if (!validMusicId(id.value) || seen.contains(id.value))
            return false;
        seen.insert(id.value);
    }
    return true;
}
template <class Id>
bool permutation(const QList<Id>& before, const QList<Id>& after, qsizetype maximum)
{
    if (before.size() != after.size() || !uniqueIds(before, maximum) || !uniqueIds(after, maximum))
        return false;
    QSet<QString> original;
    for (const auto& id : before)
        original.insert(id.value);
    for (const auto& id : after)
        if (!original.contains(id.value))
            return false;
    return true;
}
} // namespace playlist_order_detail
// Factory validation is structural evidence, NOT a cryptographic origin/permission proof.
// Only stripped IDs/context travel through the write mailbox; never the full detail DTO.
class PlaylistSongOrder
{
  public:
    static constexpr qsizetype MaximumIds = 1000;
    [[nodiscard]] static std::optional<PlaylistSongOrder>
    fromDetails(SessionToken context, const PlaylistDetails& source, QList<SongId> ordered)
    {
        if (!context.isValid() || !validMusicId(source.id.value) || !source.trackIds ||
            !source.trackIdsComplete ||
            source.trackCount != std::optional<qint64>(source.trackIds->size()) ||
            !playlist_order_detail::permutation(*source.trackIds, ordered, MaximumIds))
            return std::nullopt;
        return PlaylistSongOrder(context, source.id, std::move(ordered));
    }
    [[nodiscard]] bool isValid() const
    {
        return m_context.isValid() && validMusicId(m_playlist.value) &&
               playlist_order_detail::uniqueIds(m_ids, MaximumIds);
    }
    [[nodiscard]] SessionToken sourceContext() const { return m_context; }
    [[nodiscard]] const PlaylistId& playlist() const { return m_playlist; }
    [[nodiscard]] const QList<SongId>& ids() const { return m_ids; }

  private:
    PlaylistSongOrder(SessionToken context, PlaylistId playlist, QList<SongId> ids)
        : m_context(context), m_playlist(std::move(playlist)), m_ids(std::move(ids))
    {
    }
    SessionToken m_context;
    PlaylistId m_playlist;
    QList<SongId> m_ids;
};
class AccountPlaylistOrder
{
  public:
    static constexpr qsizetype MaximumIds = 100;
    [[nodiscard]] static std::optional<AccountPlaylistOrder>
    fromPage(SessionToken context, const MyPlaylistsPage& source, QList<PlaylistId> ordered)
    {
        if (!context.isValid() || !source.query.isValid() || source.query.page.offset != 0 ||
            source.playlists.size() > source.query.page.limit ||
            source.hasMore != std::optional<bool>(false) ||
            source.total != std::optional<qint64>(source.playlists.size()))
            return std::nullopt;
        QList<PlaylistId> before;
        for (const auto& row : source.playlists)
            before.append(row.id);
        if (!playlist_order_detail::permutation(before, ordered, MaximumIds))
            return std::nullopt;
        return AccountPlaylistOrder(context, source.query.accountId, std::move(ordered));
    }
    [[nodiscard]] bool isValid() const
    {
        return m_context.isValid() && validMusicId(m_account) &&
               playlist_order_detail::uniqueIds(m_ids, MaximumIds);
    }
    [[nodiscard]] SessionToken sourceContext() const { return m_context; }
    [[nodiscard]] const QString& sourceAccount() const { return m_account; }
    [[nodiscard]] const QList<PlaylistId>& ids() const { return m_ids; }

  private:
    AccountPlaylistOrder(SessionToken context, QString account, QList<PlaylistId> ids)
        : m_context(context), m_account(std::move(account)), m_ids(std::move(ids))
    {
    }
    SessionToken m_context;
    QString m_account;
    QList<PlaylistId> m_ids;
};
} // namespace cirrus::domain
