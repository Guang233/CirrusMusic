#include "discovery_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
MusicResult mapDiscoveryResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (std::holds_alternative<PersonalFmQuery>(query)) {
        const auto values = object.value(QStringLiteral("data"));
        if (!values.isArray() || values.toArray().size() > 20)
            return invalid;
        PersonalFmBatch batch;
        for (const auto value : values.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto song = mapping::songFrom(value.toObject());
            if (!song)
                return invalid;
            batch.songs.append(*song);
        }
        return batch;
    }
    if (!std::holds_alternative<DailyRecommendationsQuery>(query) ||
        !object.value(QStringLiteral("data")).isObject())
        return invalid;
    const auto data = object.value(QStringLiteral("data")).toObject();
    const auto values = data.value(QStringLiteral("dailySongs"));
    if (!values.isArray() || values.toArray().size() > 500)
        return invalid;
    DailyRecommendations daily;
    for (const auto value : values.toArray()) {
        if (!value.isObject())
            return invalid;
        const auto o = value.toObject();
        const auto song = mapping::songFrom(o);
        const auto reason = o.value(QStringLiteral("reason"));
        if (!song || !mapping::textField(reason))
            return invalid;
        daily.songs.append(
            {*song, reason.isString() ? std::optional<QString>(reason.toString()) : std::nullopt});
    }
    const auto reasons = data.value(QStringLiteral("recommendReasons"));
    if (!reasons.isUndefined() && !reasons.isNull()) {
        if (!reasons.isArray() || reasons.toArray().size() > 500)
            return invalid;
        for (const auto value : reasons.toArray()) {
            if (!value.isObject())
                return invalid;
            const auto o = value.toObject();
            const auto id = mapping::providerId(o.value(QStringLiteral("songId")));
            const auto reason = o.value(QStringLiteral("reason"));
            if (id.isEmpty() || !reason.isString() || !mapping::textField(reason))
                return invalid;
            daily.reasons.append({{id}, reason.toString()});
        }
    }
    return daily; // receipt time is stamped by gateway, never invented as a provider day
}
} // namespace cirrus::netease
