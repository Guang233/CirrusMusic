#pragma once
#include "catalog_types.h"

namespace cirrus::domain
{
enum class ChartListKind { Overview, WithPreviews };
struct ChartsQuery {
    ChartListKind kind = ChartListKind::Overview;
    [[nodiscard]] bool isValid() const
    {
        return kind == ChartListKind::Overview || kind == ChartListKind::WithPreviews;
    }
};
// Provider display text, NOT Song metadata/IDs. Never enqueue these strings.
struct ChartPreview {
    QString primaryText;
    std::optional<QString> secondaryText;
};
struct Chart {
    static constexpr qsizetype MaximumPreviews = 10;
    PlaylistId playlist;
    QString title, description;
    QUrl cover;
    std::optional<qint64> trackCount, providerUpdateTime; // raw updateTime; no clock inference
    std::optional<QString> updateFrequency;               // provider label, not a refresh schedule
    std::optional<QList<ChartPreview>> previews;          // missing != known empty; not all songs
};
struct Charts {
    static constexpr qsizetype MaximumCharts = 200;
    ChartsQuery query;
    QList<Chart> charts; // preserve provider order/duplicates, no invented categories/total
};
} // namespace cirrus::domain
