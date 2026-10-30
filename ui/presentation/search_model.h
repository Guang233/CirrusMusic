#pragma once

#include "request.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class CollectionResults;
class SongResults;

// The search page (docs/ui/presentation.md §5.2). `text` follows the search bar;
// suggestions follow it after a 200 ms pause, a newer text cancelling the older
// request. submit() runs a query; each result tab loads when it is first shown.
//   SearchModel { id: search; text: bar.text; tab: tabs.currentIndex }
class SearchModel : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("search_results.h")
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QStringList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(QStringList hotKeywords READ hotKeywords NOTIFY hotKeywordsChanged)
    // The submitted query; empty before the first search.
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY tabChanged) // SearchModel.Tab
    Q_PROPERTY(cirrus::ui::SongResults* songs READ songs CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* artists READ artists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionResults* playlists READ playlists CONSTANT)

  public:
    enum Tab { Songs, Albums, Artists, Playlists };
    Q_ENUM(Tab)

    explicit SearchModel(QObject* parent = nullptr);

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString& text);
    [[nodiscard]] QStringList suggestions() const { return m_suggestions; }
    [[nodiscard]] QStringList hotKeywords() const { return m_hotKeywords; }
    [[nodiscard]] QString query() const { return m_query; }
    [[nodiscard]] int tab() const { return m_tab; }
    void setTab(int tab);
    [[nodiscard]] SongResults* songs() const { return m_songs; }
    [[nodiscard]] CollectionResults* albums() const { return m_albums; }
    [[nodiscard]] CollectionResults* artists() const { return m_artists; }
    [[nodiscard]] CollectionResults* playlists() const { return m_playlists; }

    // Searches for `query` (trimmed; empty is ignored).
    Q_INVOKABLE void submit(const QString& query);

  signals:
    void textChanged();
    void suggestionsChanged();
    void hotKeywordsChanged();
    void queryChanged();
    void tabChanged();

  private:
    void requestSuggestions();
    void setSuggestions(const QStringList& suggestions);
    void loadTab();

    QString m_text;
    QString m_query;
    int m_tab = Songs;
    QStringList m_suggestions;
    QStringList m_hotKeywords;
    QTimer m_pause;
    Request m_suggestionRequest;
    Request m_hotRequest;
    SongResults* m_songs;
    CollectionResults* m_albums;
    CollectionResults* m_artists;
    CollectionResults* m_playlists;
};

} // namespace cirrus::ui
