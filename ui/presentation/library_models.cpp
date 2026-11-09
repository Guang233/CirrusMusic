#include "library_models.h"

#include "app.h"
#include "collection_list_model.h"
#include "formatter.h"
#include "song_list_model.h"

namespace cirrus::ui
{

namespace
{
constexpr int kRecentLimit = 100;

QStringList keysOf(const QList<SongInfo>& songs)
{
    QStringList keys;
    for (const SongInfo& song : songs)
        keys << song.key;
    return keys;
}
} // namespace

LibraryCollectionsModel::LibraryCollectionsModel(collection::Kind kind, QObject* parent)
    : PagedModel(parent), m_kind(kind), m_items(new CollectionListModel(this))
{
    followAccount();
}

void LibraryCollectionsModel::ensureLoaded()
{
    if (state() == loadstate::Idle)
        reload();
}

void LibraryCollectionsModel::setFilter(int filter)
{
    if (filter < All || filter > Saved || filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    show();
}

bool LibraryCollectionsModel::passes(const CollectionItem& item) const
{
    return m_filter == All || item.created == (m_filter == Created);
}

// The account's own playlists don't name their creator (it is the account).
CollectionItem LibraryCollectionsModel::shown(CollectionItem item)
{
    if (item.created.value_or(false))
        item.byline.clear();
    return item;
}

// The loaded items that pass the filter, from scratch (a new filter or first page).
void LibraryCollectionsModel::show()
{
    QList<CollectionItem> items;
    for (const CollectionItem& item : std::as_const(m_all))
        if (passes(item))
            items << shown(item);
    m_items->reset(items);
}

void LibraryCollectionsModel::clear()
{
    resetPaging();
    m_all.clear();
    m_items->clear();
}

Request LibraryCollectionsModel::loadPage(PageRequest page)
{
    auto* source = App::instance()->librarySource();
    if (page.offset == 0)
        return source->libraryCollections(
            m_kind, page,
            handle<ResultPage<CollectionItem>>([this](ResultPage<CollectionItem> result) {
                m_all = result.items;
                show();
                setTotal(result.total);
                pageArrived(int(result.items.size()), result.hasMore);
                return result.items.isEmpty();
            }));
    return source->libraryCollections(
        m_kind, page, handleMore<CollectionItem>([this](QList<CollectionItem> items) {
            // Appended, so the cards already shown stay.
            m_all << items;
            QList<CollectionItem> more;
            for (const CollectionItem& item : std::as_const(items))
                if (passes(item))
                    more << shown(item);
            m_items->append(more);
        }));
}

LibraryOverviewModel::LibraryOverviewModel(QObject* parent)
    : LoadableModel(parent), m_playlists(new LibraryCollectionsModel(collection::Playlist, this)),
      m_albums(new LibraryCollectionsModel(collection::Album, this)),
      m_artists(new LibraryCollectionsModel(collection::Artist, this)),
      m_podcasts(new LibraryCollectionsModel(collection::Podcast, this))
{
    followAccount();
    reload();
}

// The page settles with the last song played, which also tells a guest apart: only
// SignedOut fails the page, any other failure leaves that line out (the tabs load on
// their own). This week's top song follows on its own and is left out if it fails.
Request LibraryOverviewModel::load()
{
    auto* source = App::instance()->librarySource();
    m_ranking = source->listeningRanking(false, [this](Outcome<QList<SongInfo>> outcome) {
        m_ranking.release();
        if (!outcome.value || outcome.value->isEmpty())
            return;
        m_weekTop = outcome.value->constFirst().title;
        emit summaryChanged();
    });
    auto settle = handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
        m_lastPlayed = songs.isEmpty() ? QString() : songs.first().title;
        emit summaryChanged();
        return false;
    });
    return source->recentSongs(1, [settle = std::move(settle)](Outcome<QList<SongInfo>> outcome) {
        if (outcome.failure && outcome.failure->kind != failure::SignedOut)
            outcome = {QList<SongInfo>{}};
        settle(std::move(outcome));
    });
}

void LibraryOverviewModel::clear()
{
    m_ranking.cancel();
    m_lastPlayed.clear();
    m_weekTop.clear();
    emit summaryChanged();
}

void LibraryOverviewModel::createPlaylist(const QString& title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || creating())
        return;
    m_create =
        App::instance()->librarySource()->createPlaylist(trimmed, [this](Outcome<QString> outcome) {
            m_create.release();
            emit creatingChanged();
            if (!outcome.value) {
                const bool signedOut =
                    outcome.failure && outcome.failure->kind == failure::SignedOut;
                emit notice(signedOut             ? tr("登录后才能新建歌单")
                            : maybeSaved(outcome) ? tr("可能没有新建成功，刷新看看")
                                                  : tr("没能新建歌单，请稍后再试"));
                return;
            }
            if (m_playlists->state() != loadstate::Idle)
                m_playlists->refresh();
            emit libraryChanged();
            emit playlistCreated(*outcome.value);
        });
    emit creatingChanged();
}

RecentCollectionsModel::RecentCollectionsModel(collection::Kind kind, QObject* parent)
    : LoadableModel(parent), m_kind(kind), m_items(new CollectionListModel(this))
{
    followAccount();
}

void RecentCollectionsModel::ensureLoaded()
{
    if (state() == loadstate::Idle)
        reload();
}

Request RecentCollectionsModel::load()
{
    return App::instance()->librarySource()->recentCollections(
        m_kind, kRecentLimit, handle<QList<CollectionItem>>([this](QList<CollectionItem> items) {
            m_items->reset(items);
            return items.isEmpty();
        }));
}

void RecentCollectionsModel::clear() { m_items->clear(); }

RecentVoicesModel::RecentVoicesModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
}

void RecentVoicesModel::ensureLoaded()
{
    if (state() == loadstate::Idle)
        reload();
}

Request RecentVoicesModel::load()
{
    return App::instance()->librarySource()->recentVoices(
        kRecentLimit, handle<QList<SongInfo>>([this](QList<SongInfo> voices) {
            m_songs->reset(keysOf(voices), voices);
            return voices.isEmpty();
        }));
}

void RecentVoicesModel::clear() { m_songs->clear(); }

RecentPageModel::RecentPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this)),
      m_playlists(new RecentCollectionsModel(collection::Playlist, this)),
      m_albums(new RecentCollectionsModel(collection::Album, this)),
      m_podcasts(new RecentCollectionsModel(collection::Podcast, this)),
      m_voices(new RecentVoicesModel(this))
{
    followAccount();
    reload();
}

QString RecentPageModel::countText() const
{
    const int count = m_songs->rowCount();
    return count > 0 ? tr("%1 首").arg(count) : QString();
}

Request RecentPageModel::load()
{
    return App::instance()->librarySource()->recentSongs(
        kRecentLimit, handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            m_songs->reset(keysOf(songs), songs);
            emit songsChanged();
            // The other tabs still have their content.
            return false;
        }));
}

void RecentPageModel::clear()
{
    m_songs->clear();
    emit songsChanged();
}

RankingPageModel::RankingPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
    reload();
}

void RankingPageModel::setAllTime(bool allTime)
{
    if (allTime == m_allTime)
        return;
    m_allTime = allTime;
    emit allTimeChanged();
    // The other period replaces the list in place.
    if (state() == loadstate::Idle)
        reload();
    else
        refresh();
}

QString RankingPageModel::countText() const
{
    const int count = m_songs->rowCount();
    return count > 0 ? tr("%1 首").arg(count) : QString();
}

Request RankingPageModel::load()
{
    return App::instance()->librarySource()->listeningRanking(
        m_allTime, handle<QList<SongInfo>>([this](QList<SongInfo> songs) {
            m_songs->reset(keysOf(songs), songs);
            emit songsChanged();
            return songs.isEmpty();
        }));
}

void RankingPageModel::clear()
{
    m_songs->clear();
    emit songsChanged();
}

} // namespace cirrus::ui
