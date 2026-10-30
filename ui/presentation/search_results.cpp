#include "search_results.h"

#include "app.h"
#include "collection_list_model.h"
#include "song_list_model.h"

namespace cirrus::ui
{

SongResults::SongResults(QObject* parent) : PagedModel(parent), m_songs(new SongListModel(this)) {}

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
    if (page.offset == 0)
        return catalog->searchSongs(
            m_query, page, handle<ResultPage<SongInfo>>([this](ResultPage<SongInfo> result) {
                QStringList keys;
                for (const SongInfo& song : std::as_const(result.items))
                    keys << song.key;
                m_songs->reset(keys, result.items);
                pageArrived(int(result.items.size()), result.hasMore);
                return result.items.isEmpty();
            }));
    return catalog->searchSongs(m_query, page, handleMore<SongInfo>([this](QList<SongInfo> songs) {
                                    m_songs->append(songs);
                                }));
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
                return result.items.isEmpty();
            }));
    return catalog->searchCollections(
        m_kind, m_query, page, handleMore<CollectionItem>([this](QList<CollectionItem> items) {
            m_items->append(items);
        }));
}

int CollectionResults::loadedCount() const { return m_items->rowCount(); }

} // namespace cirrus::ui
