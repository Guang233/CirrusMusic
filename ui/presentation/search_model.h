#pragma once

#include "request.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class CollectionResults;
class SearchOverview;
class SongResults;

// The search field's list and the search page (docs/ui/pages/search.md). `text`
// follows the search field; suggestions follow it after a 200 ms pause, a newer text
// cancelling the older request. submit() runs a query and records it in the history;
// each result tab loads when it is first shown.
//   SearchModel { id: search; text: field.text }
class SearchModel : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("search_results.h")
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // Grouped suggestions for `text`: [{song, kind, key, title}], songs first, then
    // artists, albums and playlists, at most 3 of each.
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    // Trending searches: [{keyword, description}].
    Q_PROPERTY(QVariantList hotKeywords READ hotKeywords NOTIFY hotKeywordsChanged)
    // Recent searches, newest first (App.settings.searchHistory).
    Q_PROPERTY(QStringList history READ history NOTIFY historyChanged)
    // The submitted query; empty before the first search.
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY tabChanged) // SearchModel.Tab
    Q_PROPERTY(cirrus::ui::SearchOverview* overview READ overview CONSTANT)
    Q_PROPERTY(cirrus::ui::SongResults* songs READ songs CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* artists READ artists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* playlists READ playlists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* podcasts READ podcasts CONSTANT)
    Q_PROPERTY(cirrus::ui::SongResults* voices READ voices CONSTANT)

  public:
    enum Tab { Overview, Songs, Artists, Albums, Playlists, Podcasts, Voices };
    Q_ENUM(Tab)

    explicit SearchModel(QObject* parent = nullptr);

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString& text);
    [[nodiscard]] QVariantList suggestions() const { return m_suggestions; }
    [[nodiscard]] QVariantList hotKeywords() const { return m_hotKeywords; }
    [[nodiscard]] QStringList history() const;
    [[nodiscard]] QString query() const { return m_query; }
    [[nodiscard]] int tab() const { return m_tab; }
    void setTab(int tab);
    [[nodiscard]] SearchOverview* overview() const { return m_overview; }
    [[nodiscard]] SongResults* songs() const { return m_songs; }
    [[nodiscard]] CollectionResults* artists() const { return m_artists; }
    [[nodiscard]] CollectionResults* albums() const { return m_albums; }
    [[nodiscard]] CollectionResults* playlists() const { return m_playlists; }
    [[nodiscard]] CollectionResults* podcasts() const { return m_podcasts; }
    [[nodiscard]] SongResults* voices() const { return m_voices; }

    // Searches for `query` (trimmed; empty is ignored) and records it in the history.
    Q_INVOKABLE void submit(const QString& query);
    Q_INVOKABLE void removeHistory(const QString& query);
    Q_INVOKABLE void clearHistory();

  signals:
    void textChanged();
    void suggestionsChanged();
    void hotKeywordsChanged();
    void historyChanged();
    void queryChanged();
    void tabChanged();

  private:
    void requestSuggestions();
    void setSuggestions(const QVariantList& suggestions);
    void loadTab();

    QString m_text;
    QString m_query;
    int m_tab = Overview;
    QVariantList m_suggestions;
    QVariantList m_hotKeywords;
    QTimer m_pause;
    Request m_suggestionRequest;
    Request m_hotRequest;
    SearchOverview* m_overview;
    SongResults* m_songs;
    CollectionResults* m_artists;
    CollectionResults* m_albums;
    CollectionResults* m_playlists;
    CollectionResults* m_podcasts;
    SongResults* m_voices;
};

} // namespace cirrus::ui
