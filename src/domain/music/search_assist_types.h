#pragma once
#include "entity_search_types.h"
namespace cirrus::domain
{
struct SearchSuggestionsQuery {
    QString keywords;
    [[nodiscard]] bool isValid() const { return SearchPage{keywords, 1, 0}.isValid(); }
};
struct HotKeywordsQuery {
    [[nodiscard]] bool isValid() const { return true; }
};
struct HotKeywordDetailsQuery {
    [[nodiscard]] bool isValid() const { return true; }
};
// ID/name previews, deliberately NOT complete Song/Album/rights/playback models.
template <typename Id> struct SearchSuggestion {
    Id id;
    QString title;
};
enum class SuggestionGroup { Song, Artist, Album, Playlist };
struct SearchSuggestions {
    SearchSuggestionsQuery query;
    QList<SuggestionGroup> order; // supported groups only; duplicates retained
    bool unsupportedGroups = false;
    std::optional<QList<SearchSuggestion<SongId>>> songs;
    std::optional<QList<SearchSuggestion<ArtistId>>> artists;
    std::optional<QList<SearchSuggestion<AlbumId>>> albums;
    std::optional<QList<SearchSuggestion<PlaylistId>>> playlists;
};
struct HotKeyword {
    QString keyword;
};
struct HotKeywords {
    QList<HotKeyword> entries;
};
struct HotKeywordDetail {
    QString keyword;
    std::optional<qint64> providerScore, providerIconType; // raw values, not counts or guarantees
    std::optional<QString> description;
    QUrl icon; // display reference only, no fetch/action
};
struct HotKeywordDetails {
    QList<HotKeywordDetail> entries;
};
} // namespace cirrus::domain
