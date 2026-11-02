#include "search_model.h"

#include "app.h"
#include "app_settings.h"
#include "search_results.h"

#include <QVariantMap>

namespace cirrus::ui
{

namespace
{
constexpr int kSuggestionPauseMs = 200;
constexpr int kSuggestionsPerGroup = 3;
constexpr int kHistoryKept = 50;
} // namespace

SearchModel::SearchModel(QObject* parent)
    : QObject(parent), m_overview(new SearchOverview(this)), m_songs(new SongResults(this)),
      m_artists(new CollectionResults(collection::Artist, this)),
      m_albums(new CollectionResults(collection::Album, this)),
      m_playlists(new CollectionResults(collection::Playlist, this)),
      m_podcasts(new CollectionResults(collection::Podcast, this)),
      m_voices(new SongResults(this, true))
{
    m_pause.setSingleShot(true);
    m_pause.setInterval(kSuggestionPauseMs);
    connect(&m_pause, &QTimer::timeout, this, &SearchModel::requestSuggestions);
    connect(App::instance()->settings(), &AppSettings::searchHistoryChanged, this,
            &SearchModel::historyChanged);

    // Trending keywords are a nicety: a failure just leaves them out.
    m_hotRequest =
        App::instance()->catalog()->hotKeywords([this](Outcome<QList<HotKeyword>> outcome) {
            m_hotRequest.release();
            if (!outcome.value)
                return;
            m_hotKeywords.clear();
            for (const HotKeyword& hot : std::as_const(*outcome.value))
                m_hotKeywords << QVariantMap{{QStringLiteral("keyword"), hot.keyword},
                                             {QStringLiteral("description"), hot.description}};
            emit hotKeywordsChanged();
        });
}

void SearchModel::setText(const QString& text)
{
    if (m_text == text)
        return;
    m_text = text;
    emit textChanged();
    m_suggestionRequest.cancel();
    if (text.trimmed().isEmpty()) {
        m_pause.stop();
        setSuggestions({});
    } else {
        m_pause.start();
    }
}

void SearchModel::requestSuggestions()
{
    m_suggestionRequest = App::instance()->catalog()->suggestions(
        m_text.trimmed(), [this](Outcome<QList<SearchSuggestion>> outcome) {
            m_suggestionRequest.release();
            // Songs, then artists, albums and playlists; a few of each.
            QVariantList grouped;
            const auto group = [&](bool song, collection::Kind kind) {
                int taken = 0;
                for (const SearchSuggestion& entry :
                     outcome.value.value_or(QList<SearchSuggestion>())) {
                    if (entry.song != song || (!song && entry.kind != kind) ||
                        taken == kSuggestionsPerGroup)
                        continue;
                    ++taken;
                    grouped << QVariantMap{{QStringLiteral("song"), entry.song},
                                           {QStringLiteral("kind"), int(entry.kind)},
                                           {QStringLiteral("key"), entry.key},
                                           {QStringLiteral("title"), entry.title}};
                }
            };
            group(true, collection::Playlist);
            group(false, collection::Artist);
            group(false, collection::Album);
            group(false, collection::Playlist);
            setSuggestions(grouped);
        });
}

void SearchModel::setSuggestions(const QVariantList& suggestions)
{
    if (m_suggestions == suggestions)
        return;
    m_suggestions = suggestions;
    emit suggestionsChanged();
}

QStringList SearchModel::history() const { return App::instance()->settings()->searchHistory(); }

void SearchModel::removeHistory(const QString& query)
{
    QStringList history = this->history();
    history.removeAll(query);
    App::instance()->settings()->setSearchHistory(history);
}

void SearchModel::clearHistory() { App::instance()->settings()->setSearchHistory({}); }

void SearchModel::setTab(int tab)
{
    if (tab < Overview || tab > Voices || m_tab == tab)
        return;
    m_tab = tab;
    emit tabChanged();
    loadTab();
}

void SearchModel::submit(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;
    // Suggestions for the submitted text are no longer wanted.
    m_pause.stop();
    m_suggestionRequest.cancel();
    setSuggestions({});

    QStringList history = this->history();
    history.removeAll(trimmed);
    history.prepend(trimmed);
    App::instance()->settings()->setSearchHistory(history.mid(0, kHistoryKept));

    if (trimmed == m_query)
        return;
    m_query = trimmed;
    emit queryChanged();
    m_overview->setQuery(trimmed);
    for (SongResults* results : {m_songs, m_voices})
        results->setQuery(trimmed);
    for (CollectionResults* results : {m_artists, m_albums, m_playlists, m_podcasts})
        results->setQuery(trimmed);
    loadTab();
}

void SearchModel::loadTab()
{
    switch (m_tab) {
    case Overview:
        m_overview->ensureLoaded();
        break;
    case Songs:
        m_songs->ensureLoaded();
        break;
    case Artists:
        m_artists->ensureLoaded();
        break;
    case Albums:
        m_albums->ensureLoaded();
        break;
    case Playlists:
        m_playlists->ensureLoaded();
        break;
    case Podcasts:
        m_podcasts->ensureLoaded();
        break;
    case Voices:
        m_voices->ensureLoaded();
        break;
    }
}

} // namespace cirrus::ui
