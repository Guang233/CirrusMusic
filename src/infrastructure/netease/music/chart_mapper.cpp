#include "chart_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
using namespace mapping;
bool absent(const QJsonValue& v) { return v.isNull() || v.isUndefined(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
bool label(const QJsonValue& v, std::optional<QString>& out)
{
    if (absent(v))
        return true;
    if (!textField(v))
        return false;
    out = v.toString();
    return true;
}
} // namespace
std::optional<ApiOperation> chartOperation(const MusicQuery& query)
{
    const auto* q = std::get_if<ChartsQuery>(&query);
    if (!q || !q->isValid())
        return std::nullopt;
    return q->kind == ChartListKind::Overview ? ApiOperation::Charts
                                              : ApiOperation::ChartsWithPreviews;
}
MusicResult mapChartsResponse(const ChartsQuery& query, const QJsonObject& object)
{
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!query.isValid() || object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    const auto values = object.value(QStringLiteral("list"));
    if (!values.isArray() || values.toArray().size() > Charts::MaximumCharts)
        return invalid;
    Charts result;
    result.query = query;
    for (const auto value : values.toArray()) {
        if (!value.isObject())
            return invalid;
        const auto row = value.toObject();
        Chart chart;
        chart.playlist.value = mapping::providerId(row.value(QStringLiteral("id")));
        const auto name = row.value(QStringLiteral("name"));
        const auto description = row.value(QStringLiteral("description"));
        const auto cover = row.value(QStringLiteral("coverImgUrl"));
        if (chart.playlist.value.isEmpty() || !mapping::textField(name) ||
            (!absent(description) &&
             (!description.isString() || description.toString().size() > 65536 ||
              !description.toString().isValidUtf16() ||
              description.toString().contains(QChar::Null))))
            return invalid;
        chart.title = name.toString();
        chart.description = description.toString();
        // Same catalog image policy: unsupported URLs become unavailable metadata, never fetched.
        chart.cover = mapping::imageFrom(cover);
        if (!number(row.value(QStringLiteral("trackCount")), chart.trackCount) ||
            !number(row.value(QStringLiteral("updateTime")), chart.providerUpdateTime) ||
            !label(row.value(QStringLiteral("updateFrequency")), chart.updateFrequency))
            return invalid;
        const auto previews = row.value(QStringLiteral("tracks"));
        if (!absent(previews)) {
            if (!previews.isArray() || previews.toArray().size() > Chart::MaximumPreviews)
                return invalid;
            chart.previews = QList<ChartPreview>{};
            for (const auto preview : previews.toArray()) {
                if (!preview.isObject())
                    return invalid;
                const auto p = preview.toObject();
                const auto first = p.value(QStringLiteral("first"));
                ChartPreview item;
                if (!first.isString() || !mapping::textField(first) ||
                    !label(p.value(QStringLiteral("second")), item.secondaryText))
                    return invalid;
                item.primaryText = first.toString();
                chart.previews->append(item);
            }
        }
        result.charts.append(chart);
    }
    // artistToplist/rewardToplist and unrelated response sections are not playlist charts.
    return result;
}
} // namespace cirrus::netease
