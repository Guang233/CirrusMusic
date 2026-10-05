#include "cloud_history_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <cmath>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& value) { return value.isNull() || value.isUndefined(); }
bool number(const QJsonValue& value, std::optional<qint64>& out)
{
    if (absent(value))
        return true;
    out = nonnegative(value);
    return out.has_value();
}
} // namespace
std::optional<CloudHistoryRequest> cloudHistoryRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<RecentSongsQuery>(&query))
        return CloudHistoryRequest{ApiOperation::RecentSongs,
                                   {{QStringLiteral("limit"), q->limit}}};
    if (const auto* q = std::get_if<ListeningRankingQuery>(&query))
        return CloudHistoryRequest{
            ApiOperation::ListeningRanking,
            {{QStringLiteral("uid"), q->accountId},
             {QStringLiteral("type"), q->period == RankingPeriod::LastWeek ? 1 : 0}}};
    return std::nullopt;
}
MusicResult mapCloudHistoryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    const auto account = historyQueryAccount(query);
    if (!account || !validMusicQuery(query) ||
        object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    const auto uid = object.value(QStringLiteral("uid"));
    if (!absent(uid) && providerId(uid) != *account)
        return invalid;
    if (const auto* q = std::get_if<RecentSongsQuery>(&query)) {
        const auto raw = object.value(QStringLiteral("data"));
        if (!raw.isObject())
            return invalid;
        const auto data = raw.toObject();
        const auto values = data.value(QStringLiteral("list"));
        CloudRecentSongs result;
        result.query = *q;
        if (!values.isArray() || values.toArray().size() > q->limit ||
            !number(data.value(QStringLiteral("total")), result.total) ||
            (result.total && *result.total < values.toArray().size()))
            return invalid;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto row = value.toObject();
            const auto rawSong = row.value(QStringLiteral("data"));
            if (!rawSong.isObject())
                return invalid;
            const auto song = songFrom(rawSong.toObject());
            if (!song)
                return invalid;
            const auto resourceId = row.value(QStringLiteral("resourceId"));
            const auto type = row.value(QStringLiteral("resourceType"));
            if ((!absent(resourceId) && providerId(resourceId) != song->id.value) ||
                (!absent(type) && type != QJsonValue(QStringLiteral("SONG"))))
                return invalid;
            RecentSong item;
            item.song = *song;
            if (!number(row.value(QStringLiteral("playTime")), item.providerPlayTime))
                return invalid;
            result.songs.append(item); // duplicates and server ordering are intentional
        }
        return result;
    }
    if (const auto* q = std::get_if<ListeningRankingQuery>(&query)) {
        const auto values =
            object.value(q->period == RankingPeriod::LastWeek ? QStringLiteral("weekData")
                                                              : QStringLiteral("allData"));
        if (!values.isArray() || values.toArray().size() > ListeningRanking::MaximumSongs)
            return invalid;
        ListeningRanking result;
        result.query = *q;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto row = value.toObject();
            const auto rawSong = row.value(QStringLiteral("song"));
            if (!rawSong.isObject())
                return invalid;
            const auto song = songFrom(rawSong.toObject());
            if (!song)
                return invalid;
            RankedSong item;
            item.song = *song;
            if (!number(row.value(QStringLiteral("playCount")), item.playCount))
                return invalid;
            const auto score = row.value(QStringLiteral("score"));
            if (!absent(score)) {
                if (!score.isDouble() || !std::isfinite(score.toDouble()) || score.toDouble() < 0 ||
                    score.toDouble() > 9007199254740991.0)
                    return invalid;
                item.score = score.toDouble();
            }
            result.songs.append(item);
        }
        return result;
    }
    return invalid;
}
} // namespace cirrus::netease
