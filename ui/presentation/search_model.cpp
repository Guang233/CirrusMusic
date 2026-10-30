#include "search_model.h"

#include "app.h"
#include "search_results.h"

namespace cirrus::ui
{

namespace
{
constexpr int kSuggestionPauseMs = 200;
} // namespace

SearchModel::SearchModel(QObject* parent)
    : QObject(parent), m_songs(new SongResults(this)),
      m_albums(new CollectionResults(collection::Album, this)),
      m_artists(new CollectionResults(collection::Artist, this)),
      m_playlists(new CollectionResults(collection::Playlist, this))
{
    m_pause.setSingleShot(true);
    m_pause.setInterval(kSuggestionPauseMs);
    connect(&m_pause, &QTimer::timeout, this, &SearchModel::requestSuggestions);

    // Trending keywords are a nicety: a failure just leaves them out.
    m_hotRequest = App::instance()->catalog()->hotKeywords([this](Outcome<QStringList> outcome) {
        m_hotRequest.release();
        if (!outcome.value)
            return;
        m_hotKeywords = *outcome.value;
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
        m_text.trimmed(), [this](Outcome<QStringList> outcome) {
            m_suggestionRequest.release();
            setSuggestions(outcome.value.value_or(QStringList()));
        });
}

void SearchModel::setSuggestions(const QStringList& suggestions)
{
    if (m_suggestions == suggestions)
        return;
    m_suggestions = suggestions;
    emit suggestionsChanged();
}

void SearchModel::setTab(int tab)
{
    if (tab < Songs || tab > Playlists || m_tab == tab)
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
    m_query = trimmed;
    emit queryChanged();
    m_songs->setQuery(trimmed);
    m_albums->setQuery(trimmed);
    m_artists->setQuery(trimmed);
    m_playlists->setQuery(trimmed);
    loadTab();
}

void SearchModel::loadTab()
{
    switch (m_tab) {
    case Songs:
        m_songs->ensureLoaded();
        break;
    case Albums:
        m_albums->ensureLoaded();
        break;
    case Artists:
        m_artists->ensureLoaded();
        break;
    case Playlists:
        m_playlists->ensureLoaded();
        break;
    }
}

} // namespace cirrus::ui
