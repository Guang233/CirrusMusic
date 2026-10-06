#include "search_assist_mapper.h"
#include "metadata_mapping.h"
#include <QJsonArray>
namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
template <typename Id>
bool previews(const QJsonValue& raw, std::optional<QList<SearchSuggestion<Id>>>& out)
{
    if (absent(raw))
        return true;
    if (!raw.isArray() || raw.toArray().size() > 20)
        return false;
    QList<SearchSuggestion<Id>> entries;
    for (const auto v : raw.toArray()) {
        if (!v.isObject())
            return false;
        const auto o = v.toObject();
        const auto id = providerId(o.value(QStringLiteral("id")));
        std::optional<QString> title;
        if (id.isEmpty() || !metadataText(o.value(QStringLiteral("name")), title) || !title)
            return false;
        entries.append({Id{id}, *title});
    }
    out = std::move(entries);
    return true;
}
bool rawNumber(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
bool keyword(const QJsonValue& v, QString& out)
{
    if (!v.isString())
        return false;
    out = v.toString();
    return SearchSuggestionsQuery{out}.isValid();
}
} // namespace
std::optional<SearchAssistRequest> searchAssistRequest(const MusicQuery& query)
{
    if (!validMusicQuery(query))
        return std::nullopt;
    if (const auto* q = std::get_if<SearchSuggestionsQuery>(&query))
        return SearchAssistRequest{ApiOperation::SearchSuggestions,
                                   {{QStringLiteral("keywords"), q->keywords}}};
    if (std::holds_alternative<HotKeywordsQuery>(query))
        return SearchAssistRequest{ApiOperation::HotKeywords, {}};
    if (std::holds_alternative<HotKeywordDetailsQuery>(query))
        return SearchAssistRequest{ApiOperation::HotKeywordDetails, {}};
    return std::nullopt;
}
MusicResult mapSearchAssistResponse(const MusicQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!validMusicQuery(query) || object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<SearchSuggestionsQuery>(&query)) {
        const auto raw = object.value(QStringLiteral("result"));
        if (!raw.isObject())
            return invalid;
        const auto o = raw.toObject();
        SearchSuggestions result;
        result.query = *q;
        if (!previews(o.value(QStringLiteral("songs")), result.songs) ||
            !previews(o.value(QStringLiteral("artists")), result.artists) ||
            !previews(o.value(QStringLiteral("albums")), result.albums) ||
            !previews(o.value(QStringLiteral("playlists")), result.playlists))
            return invalid;
        const auto order = o.value(QStringLiteral("order"));
        if (!order.isArray() || order.toArray().size() > 16)
            return invalid;
        for (const auto v : order.toArray()) {
            std::optional<QString> group;
            if (!metadataText(v, group, 64) || !group || group->isEmpty())
                return invalid;
            if (*group == QStringLiteral("songs")) {
                if (!result.songs)
                    return invalid;
                result.order.append(SuggestionGroup::Song);
            } else if (*group == QStringLiteral("artists")) {
                if (!result.artists)
                    return invalid;
                result.order.append(SuggestionGroup::Artist);
            } else if (*group == QStringLiteral("albums")) {
                if (!result.albums)
                    return invalid;
                result.order.append(SuggestionGroup::Album);
            } else if (*group == QStringLiteral("playlists")) {
                if (!result.playlists)
                    return invalid;
                result.order.append(SuggestionGroup::Playlist);
            } else
                result.unsupportedGroups = true;
        }
        return result;
    }
    if (std::holds_alternative<HotKeywordsQuery>(query)) {
        const auto container = object.value(QStringLiteral("result"));
        if (!container.isObject())
            return invalid;
        const auto raw = container.toObject().value(QStringLiteral("hots"));
        if (!raw.isArray() || raw.toArray().size() > 100)
            return invalid;
        HotKeywords result;
        for (const auto v : raw.toArray()) {
            if (!v.isObject())
                return invalid;
            HotKeyword entry;
            if (!keyword(v.toObject().value(QStringLiteral("first")), entry.keyword))
                return invalid;
            result.entries.append(std::move(entry));
        }
        return result;
    }
    if (std::holds_alternative<HotKeywordDetailsQuery>(query)) {
        const auto raw = object.value(QStringLiteral("data"));
        if (!raw.isArray() || raw.toArray().size() > 100)
            return invalid;
        HotKeywordDetails result;
        for (const auto v : raw.toArray()) {
            if (!v.isObject())
                return invalid;
            const auto o = v.toObject();
            HotKeywordDetail entry;
            if (!keyword(o.value(QStringLiteral("searchWord")), entry.keyword) ||
                !rawNumber(o.value(QStringLiteral("score")), entry.providerScore) ||
                !rawNumber(o.value(QStringLiteral("iconType")), entry.providerIconType) ||
                !metadataText(o.value(QStringLiteral("content")), entry.description))
                return invalid;
            entry.icon = imageFrom(o.value(QStringLiteral("iconUrl")));
            result.entries.append(std::move(entry));
        }
        return result;
    }
    return invalid;
}
} // namespace cirrus::netease
