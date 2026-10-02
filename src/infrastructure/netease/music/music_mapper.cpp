#include "music_mapper.h"
#include "catalog_mapper.h"
#include "lyric_parser.h"
#include "music_mapping.h"

#include <QJsonArray>
#include <QSet>
#include <QVariant>
#include <algorithm>

namespace cirrus::netease
{
namespace mapping
{
using namespace domain;

QString providerId(const QJsonValue& value)
{
    if (value.isString())
        return validMusicId(value.toString()) ? value.toString() : QString{};
    if (value.toVariant().metaType().id() == QMetaType::LongLong) {
        const auto id = value.toInteger();
        if (id > 0 && id <= 9007199254740991LL)
            return QString::number(id);
    }
    return {};
}

std::optional<qint64> nonnegative(const QJsonValue& value)
{
    if (value.toVariant().metaType().id() != QMetaType::LongLong)
        return std::nullopt;
    const auto number = value.toInteger();
    return number >= 0 && number <= 9007199254740991LL ? std::optional<qint64>(number)
                                                       : std::nullopt;
}

bool textField(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull() ||
           (value.isString() && value.toString().size() <= 4096 &&
            value.toString().isValidUtf16() && !value.toString().contains(QChar::Null));
}

bool unknownId(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull() || value == QJsonValue(0);
}

std::optional<Song> songFrom(const QJsonObject& object)
{
    Song song;
    song.id = {providerId(object.value(QStringLiteral("id")))};
    const auto title = object.value(QStringLiteral("name"));
    if (song.id.value.isEmpty() || !textField(title))
        return std::nullopt;
    song.title = title.toString();
    const auto artists = object.contains(QStringLiteral("ar"))
                             ? object.value(QStringLiteral("ar"))
                             : object.value(QStringLiteral("artists"));
    if ((!artists.isArray() && !artists.isUndefined() && !artists.isNull()) ||
        artists.toArray().size() > 100)
        return std::nullopt;
    for (const auto value : artists.toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const auto artist = value.toObject();
        const auto name = artist.value(QStringLiteral("name"));
        const auto rawId = artist.value(QStringLiteral("id"));
        const auto id = providerId(rawId);
        if (!textField(name) || (id.isEmpty() && !unknownId(rawId)))
            return std::nullopt;
        song.artists.append(
            {id.isEmpty() ? std::nullopt : std::optional<ArtistId>({id}), name.toString()});
    }
    const auto album = object.contains(QStringLiteral("al"))
                           ? object.value(QStringLiteral("al"))
                           : object.value(QStringLiteral("album"));
    if (album.isObject()) {
        const auto a = album.toObject();
        const auto id = providerId(a.value(QStringLiteral("id")));
        const auto title = a.value(QStringLiteral("name"));
        if (!textField(title) || (id.isEmpty() && !unknownId(a.value(QStringLiteral("id")))))
            return std::nullopt;
        const auto coverText = a.value(QStringLiteral("picUrl")).toString();
        auto cover = QUrl(coverText, QUrl::StrictMode);
        if (coverText.size() > 2048 || !cover.isValid() || cover.host().isEmpty() ||
            !cover.userInfo().isEmpty() ||
            (cover.scheme() != QStringLiteral("https") && cover.scheme() != QStringLiteral("http")))
            cover = QUrl{};
        song.album = AlbumReference{id.isEmpty() ? std::nullopt : std::optional<AlbumId>({id}),
                                    title.toString(), cover};
    } else if (!album.isUndefined() && !album.isNull())
        return std::nullopt;
    const auto duration = object.contains(QStringLiteral("dt"))
                              ? object.value(QStringLiteral("dt"))
                              : object.value(QStringLiteral("duration"));
    if (!duration.isNull() && !duration.isUndefined()) {
        song.durationMs = nonnegative(duration);
        if (!song.durationMs || *song.durationMs > 7LL * 24 * 60 * 60 * 1000)
            return std::nullopt;
    }
    return song;
}

} // namespace mapping
namespace
{
using namespace domain;
using namespace application;
using namespace mapping;
LyricTrack lyricField(const QJsonObject& object, const QString& name)
{
    const auto value = object.value(name);
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isObject() || !value.toObject().value(QStringLiteral("lyric")).isString())
        return LyricTrack{LyricAvailability::Malformed};
    return parseLyricText(value.toObject().value(QStringLiteral("lyric")).toString());
}
} // namespace

application::MusicResult mapMusicResponse(const application::MusicQuery& query,
                                          const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* lyrics = std::get_if<LyricQuery>(&query)) {
        SongLyrics document;
        document.song = lyrics->song;
        document.original = {lyricField(object, QStringLiteral("lrc")),
                             lyricField(object, QStringLiteral("yrc"))};
        document.translation = {lyricField(object, QStringLiteral("tlyric")),
                                lyricField(object, QStringLiteral("ytlrc"))};
        document.romanization = {lyricField(object, QStringLiteral("romalrc")),
                                 lyricField(object, QStringLiteral("yromalrc"))};
        const auto noLyric = object.value(QStringLiteral("nolyric"));
        const auto pure = object.value(QStringLiteral("pureMusic"));
        if (noLyric.isBool())
            document.providerNoLyric = noLyric.toBool();
        if (pure.isBool())
            document.instrumental = pure.toBool();
        return document;
    }
    if (!std::holds_alternative<SongSearch>(query) && !std::holds_alternative<DetailQuery>(query))
        return mapCatalogResponse(query, object);
    QJsonValue values;
    SongSearchPage page;
    if (const auto* search = std::get_if<SongSearch>(&query)) {
        page.query = *search;
        if (!object.value(QStringLiteral("result")).isObject())
            return invalid;
        const auto result = object.value(QStringLiteral("result")).toObject();
        const auto count = result.value(QStringLiteral("songCount"));
        if (!count.isUndefined()) {
            page.total = nonnegative(count);
            if (!page.total)
                return invalid;
        }
        const auto more = result.value(QStringLiteral("hasMore"));
        if (more.isBool())
            page.hasMore = more.toBool();
        values = result.value(QStringLiteral("songs"));
        if (values.isUndefined() && page.total == 0)
            return page;
    } else
        values = object.value(QStringLiteral("songs"));
    const auto maxItems = std::holds_alternative<SongSearch>(query) ? page.query.limit : 100;
    if (!values.isArray() || values.toArray().size() > maxItems)
        return invalid;
    QSet<QString> seen;
    QList<Song> songs;
    for (const auto value : values.toArray()) {
        if (!value.isObject())
            return invalid;
        const auto song = songFrom(value.toObject());
        if (!song || seen.contains(song->id.value))
            return invalid;
        seen.insert(song->id.value);
        songs.append(*song);
    }
    if (std::holds_alternative<SongSearch>(query)) {
        page.songs = std::move(songs);
        return page;
    }
    SongDetails details;
    const auto& requested = std::get<DetailQuery>(query).songs;
    for (const auto& id : requested) {
        const auto it = std::find_if(songs.cbegin(), songs.cend(),
                                     [&](const Song& song) { return song.id == id; });
        if (it == songs.cend())
            details.missing.append(id);
        else
            details.songs.append(*it);
    }
    if (details.songs.size() != songs.size())
        return invalid; // unsolicited IDs are not another request's answer
    return details;
}
} // namespace cirrus::netease
