#include "search_results.h"

#include "app.h"
#include "collection_list_model.h"
#include "formatter.h"
#include "song_list_model.h"

#include <array>
#include <memory>

namespace cirrus::ui
{

SongResults::SongResults(QObject* parent, bool voices)
    : PagedModel(parent), m_voices(voices), m_songs(new SongListModel(this))
{
}

void SongResults::setQuery(const QString& query)
{
    m_query = query;
    reset();
}

void SongResults::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_query.isEmpty())
        reload();
}

void SongResults::clear()
{
    resetPaging();
    m_songs->clear();
}

Request SongResults::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    const auto search = [&](Reply<ResultPage<SongInfo>> reply) {
        return m_voices ? catalog->searchVoices(m_query, page, std::move(reply))
                        : catalog->searchSongs(m_query, page, std::move(reply));
    };
    if (page.offset == 0)
        return search(handle<ResultPage<SongInfo>>([this](ResultPage<SongInfo> result) {
            QStringList keys;
            for (const SongInfo& song : std::as_const(result.items))
                keys << song.key;
            m_songs->reset(keys, result.items);
            pageArrived(int(result.items.size()), result.hasMore);
            setTotal(result.total);
            return result.items.isEmpty();
        }));
    return search(handleMore<SongInfo>([this](QList<SongInfo> songs) { m_songs->append(songs); }));
}

int SongResults::loadedCount() const { return m_songs->rowCount(); }

CollectionResults::CollectionResults(collection::Kind kind, QObject* parent)
    : PagedModel(parent), m_kind(kind), m_items(new CollectionListModel(this))
{
}

void CollectionResults::setQuery(const QString& query)
{
    m_query = query;
    reset();
}

void CollectionResults::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_query.isEmpty())
        reload();
}

void CollectionResults::clear()
{
    resetPaging();
    m_items->clear();
}

Request CollectionResults::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (page.offset == 0)
        return catalog->searchCollections(
            m_kind, m_query, page,
            handle<ResultPage<CollectionItem>>([this](ResultPage<CollectionItem> result) {
                m_items->reset(result.items);
                pageArrived(int(result.items.size()), result.hasMore);
                setTotal(result.total);
                return result.items.isEmpty();
            }));
    return catalog->searchCollections(
        m_kind, m_query, page, handleMore<CollectionItem>([this](QList<CollectionItem> items) {
            m_items->append(items);
        }));
}

int CollectionResults::loadedCount() const { return m_items->rowCount(); }

namespace
{

constexpr int kOverviewSongs = 10;
constexpr int kOverviewCards = 6;

// Names compared for the best match: case, width (full-width letters) and the
// spaces around them don't count.
QString comparable(const QString& name)
{
    return name.normalized(QString::NormalizationForm_KC).trimmed().toCaseFolded();
}

} // namespace

SearchOverview::SearchOverview(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this)),
      m_artists(new CollectionListModel(this)), m_albums(new CollectionListModel(this)),
      m_playlists(new CollectionListModel(this))
{
}

void SearchOverview::setQuery(const QString& query)
{
    m_query = query;
    reset();
}

void SearchOverview::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_query.isEmpty())
        reload();
}

QString SearchOverview::bestSubtitle() const
{
    if (!m_best)
        return {};
    QStringList parts;
    switch (m_best->kind) {
    case collection::Artist:
        parts << tr("歌手");
        if (m_best->count >= 0)
            parts << tr("%1 张专辑").arg(Formatter::count(m_best->count));
        break;
    case collection::Album:
        parts << tr("专辑") << m_best->byline;
        if (m_best->year > 0)
            parts << QString::number(m_best->year);
        break;
    case collection::Playlist:
        parts << tr("歌单") << m_best->byline;
        if (m_best->count >= 0)
            parts << tr("%1 首").arg(Formatter::count(m_best->count));
        break;
    case collection::Podcast:
        parts << tr("播客");
        break;
    }
    parts.removeAll(QString());
    return parts.join(tr(" · "));
}

Request SearchOverview::load()
{
    // Four requests at once; the page's state settles when all have answered.
    struct Pending {
        int left = 4;
        bool anyAnswer = false;
        std::optional<Failure> failure;
        std::array<Request, 4> requests;
    };
    auto pending = std::make_shared<Pending>();
    auto done = std::make_shared<Reply<bool>>(handle<bool>([this](bool) {
        findBest();
        return m_songs->rowCount() == 0 && m_artists->rowCount() == 0 &&
               m_albums->rowCount() == 0 && m_playlists->rowCount() == 0;
    }));
    const auto finish = [pending, done](int index, const std::optional<Failure>& failure) {
        pending->requests.at(index).release();
        if (failure && !pending->failure)
            pending->failure = failure;
        pending->anyAnswer = pending->anyAnswer || !failure;
        if (--pending->left > 0)
            return;
        Outcome<bool> outcome;
        if (pending->anyAnswer)
            outcome.value = true;
        else
            outcome.failure = pending->failure;
        (*done)(std::move(outcome));
    };
    auto* catalog = App::instance()->catalog();
    pending->requests[0] = catalog->searchSongs(
        m_query, {0, kOverviewSongs}, [this, finish](Outcome<ResultPage<SongInfo>> outcome) {
            if (outcome.value) {
                QStringList keys;
                for (const SongInfo& song : std::as_const(outcome.value->items))
                    keys << song.key;
                m_songs->reset(keys, outcome.value->items);
                m_totals[0] = outcome.value->total.value_or(-1);
            }
            finish(0, outcome.failure);
        });
    const std::array<std::pair<collection::Kind, CollectionListModel*>, 3> kinds{
        {{collection::Artist, m_artists},
         {collection::Album, m_albums},
         {collection::Playlist, m_playlists}}};
    for (int i = 0; i < int(kinds.size()); ++i) {
        CollectionListModel* list = kinds.at(i).second;
        pending->requests.at(i + 1) = catalog->searchCollections(
            kinds.at(i).first, m_query, {0, kOverviewCards},
            [this, list, finish, i](Outcome<ResultPage<CollectionItem>> outcome) {
                if (outcome.value) {
                    list->reset(outcome.value->items);
                    m_totals[i + 1] = outcome.value->total.value_or(-1);
                }
                finish(i + 1, outcome.failure);
            });
    }
    return Request([pending]() {
        for (Request& request : pending->requests)
            request.cancel();
    });
}

void SearchOverview::findBest()
{
    std::optional<CollectionItem> best;
    const QString query = comparable(m_query);
    for (const CollectionListModel* list : {m_artists, m_albums, m_playlists}) {
        if (best || list->rowCount() == 0)
            continue;
        const CollectionItem item = list->itemAt(0);
        if (comparable(item.title) == query)
            best = item;
    }
    m_best = std::move(best);
    emit resultsChanged();
}

void SearchOverview::clear()
{
    m_songs->clear();
    m_artists->clear();
    m_albums->clear();
    m_playlists->clear();
    m_best.reset();
    m_totals = {-1, -1, -1, -1};
    emit resultsChanged();
}

} // namespace cirrus::ui
