#include "recommendation_history_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
std::optional<RecommendationHistoryRequest> recommendationHistoryRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (std::holds_alternative<DailyHistoryDatesQuery>(query))
        return RecommendationHistoryRequest{ApiOperation::DailyHistoryDates, {}};
    if (const auto* detail = std::get_if<DailyHistorySongsQuery>(&query))
        return RecommendationHistoryRequest{ApiOperation::DailyHistorySongs,
                                            {{QStringLiteral("date"), detail->date()}}};
    return std::nullopt;
}
MusicResult mapRecommendationHistoryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!validMusicQuery(query) || object.value(QStringLiteral("code")) != QJsonValue(200) ||
        !object.value(QStringLiteral("data")).isObject())
        return invalid;
    const auto data = object.value(QStringLiteral("data")).toObject();
    if (const auto* dates = std::get_if<DailyHistoryDatesQuery>(&query)) {
        const auto values = data.value(QStringLiteral("dates"));
        if (!values.isArray() || values.toArray().size() > DailyHistoryDates::MaximumDates)
            return invalid;
        DailyHistoryDates result{*dates, {}};
        for (const auto value : values.toArray()) {
            if (!value.isString() || !validRecommendationDate(value.toString()))
                return invalid;
            result.dates.append(value.toString());
        }
        return result;
    }
    if (const auto* detail = std::get_if<DailyHistorySongsQuery>(&query)) {
        const auto values = data.value(QStringLiteral("songs"));
        if (!values.isArray() || values.toArray().size() > 500)
            return invalid;
        DailyHistorySongs result{*detail, {}};
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto song = mapping::songFrom(value.toObject());
            if (!song)
                return invalid;
            result.songs.append(*song);
        }
        return result;
    }
    return invalid;
}
} // namespace cirrus::netease
