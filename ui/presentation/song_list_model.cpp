#include "song_list_model.h"

#include "app.h"
#include "formatter.h"
#include "player.h"

#include <QHash>
#include <QVariantMap>

namespace cirrus::ui
{

SongListModel::SongListModel(QObject* parent) : QAbstractListModel(parent)
{
    if (const App* app = App::instance()) {
        Player* player = app->player();
        m_currentKey = player->current().key;
        connect(player, &Player::currentChanged, this,
                [this, player]() { setCurrentKey(player->current().key); });
    }
}

void SongListModel::setCurrentKey(const QString& key)
{
    if (key == m_currentKey)
        return;
    const QString previous = std::exchange(m_currentKey, key);
    for (int row = 0; row < m_rows.size(); ++row) {
        const QString& rowKey = m_rows.at(row).song.key;
        if (rowKey == previous || rowKey == key)
            emit dataChanged(index(row), index(row), {CurrentRole});
    }
}

void SongListModel::reset(const QStringList& keys, const QList<SongInfo>& details)
{
    QHash<QString, const SongInfo*> byKey;
    for (const SongInfo& song : details)
        byKey.insert(song.key, &song);

    const int oldCount = rowCount();
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(keys.size());
    for (const QString& key : keys) {
        if (const SongInfo* song = byKey.value(key))
            m_rows.append({*song, true});
        else
            m_rows.append({SongInfo{.key = key}, false});
    }
    endResetModel();
    if (rowCount() != oldCount)
        emit countChanged();
}

void SongListModel::clear() { reset({}, {}); }

void SongListModel::append(const QList<SongInfo>& songs)
{
    if (songs.isEmpty())
        return;
    const int first = rowCount();
    beginInsertRows({}, first, first + int(songs.size()) - 1);
    for (const SongInfo& song : songs)
        m_rows.append({song, true});
    endInsertRows();
    emit countChanged();
}

QStringList SongListModel::keys() const
{
    QStringList keys;
    keys.reserve(m_rows.size());
    for (const Row& row : m_rows)
        keys << row.song.key;
    return keys;
}

int SongListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant SongListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row& row = m_rows.at(index.row());
    const SongInfo& song = row.song;
    switch (role) {
    case KeyRole:
        return song.key;
    case TitleRole:
        return song.title;
    case SecondaryTitleRole:
        return song.secondaryTitle;
    case ArtistsRole: {
        QVariantList artists;
        for (const ArtistRef& artist : song.artists)
            artists << QVariantMap{{QStringLiteral("key"), artist.key},
                                   {QStringLiteral("name"), artist.name}};
        return artists;
    }
    case ArtistTextRole:
        return Formatter::artists(song.artists);
    case AlbumKeyRole:
        return song.albumKey;
    case AlbumTitleRole:
        return song.albumTitle;
    case CoverKeyRole:
        return song.coverKey;
    case DurationMsRole:
        return song.durationMs;
    case DurationTextRole:
        return Formatter::duration(song.durationMs);
    case LoadedRole:
        return row.loaded;
    case UnavailableRole:
        return song.unavailable;
    case CurrentRole:
        return !song.key.isEmpty() && song.key == m_currentKey;
    default:
        return {};
    }
}

QHash<int, QByteArray> SongListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {TitleRole, "title"},
        {SecondaryTitleRole, "secondaryTitle"},
        {ArtistsRole, "artists"},
        {ArtistTextRole, "artistText"},
        {AlbumKeyRole, "albumKey"},
        {AlbumTitleRole, "albumTitle"},
        {CoverKeyRole, "coverKey"},
        {DurationMsRole, "durationMs"},
        {DurationTextRole, "durationText"},
        {LoadedRole, "loaded"},
        {UnavailableRole, "unavailable"},
        {CurrentRole, "current"},
    };
}

} // namespace cirrus::ui
