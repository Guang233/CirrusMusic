#include "discover_models.h"

#include "app.h"
#include "collection_list_model.h"

#include <QVariantMap>

namespace cirrus::ui
{

PlaylistCategoriesModel::PlaylistCategoriesModel(QObject* parent) : LoadableModel(parent)
{
    reload();
}

QVariantList PlaylistCategoriesModel::groups() const
{
    QVariantList groups;
    for (const PlaylistCategories::Group& group : m_categories.groups)
        groups << QVariantMap{{QStringLiteral("name"), group.name},
                              {QStringLiteral("categories"), group.categories}};
    return groups;
}

Request PlaylistCategoriesModel::load()
{
    return App::instance()->catalog()->playlistCategories(
        handle<PlaylistCategories>([this](PlaylistCategories categories) {
            m_categories = std::move(categories);
            emit categoriesChanged();
            return m_categories.groups.isEmpty() && m_categories.hot.isEmpty();
        }));
}

void PlaylistCategoriesModel::clear()
{
    m_categories = {};
    emit categoriesChanged();
}

DiscoverCardsModel::DiscoverCardsModel(QObject* parent)
    : PagedModel(parent), m_items(new CollectionListModel(this))
{
}

void DiscoverCardsModel::ensureLoaded()
{
    if (state() == loadstate::Idle)
        reload();
}

void DiscoverCardsModel::filterChanged()
{
    if (state() != loadstate::Idle)
        refresh();
}

void DiscoverCardsModel::clear()
{
    resetPaging();
    m_items->clear();
}

int DiscoverCardsModel::loadedCount() const { return m_items->rowCount(); }

Reply<ResultPage<CollectionItem>> DiscoverCardsModel::firstPage()
{
    return handle<ResultPage<CollectionItem>>([this](ResultPage<CollectionItem> result) {
        m_items->reset(result.items);
        setTotal(result.total);
        pageArrived(int(result.items.size()), result.hasMore);
        return result.items.isEmpty();
    });
}

Reply<ResultPage<CollectionItem>> DiscoverCardsModel::nextPage()
{
    return handleMore<CollectionItem>(
        [this](QList<CollectionItem> items) { m_items->append(items); });
}

DiscoverPlaylistsModel::DiscoverPlaylistsModel(QObject* parent) : DiscoverCardsModel(parent) {}

void DiscoverPlaylistsModel::setCategory(const QString& category)
{
    if (category == m_category)
        return;
    m_category = category;
    emit categoryChanged();
    filterChanged();
}

void DiscoverPlaylistsModel::setOrder(int order)
{
    if (order < Hot || order > Premium || order == m_order)
        return;
    m_order = order;
    emit orderChanged();
    filterChanged();
}

Request DiscoverPlaylistsModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (m_order != Premium)
        return catalog->categoryPlaylists(m_category, m_order == Newest, page,
                                          page.offset == 0 ? firstPage() : nextPage());
    // Premium pages follow a cursor; one that doesn't move on ends the list.
    const qint64 before = page.offset == 0 ? 0 : m_cursor;
    return catalog->premiumPlaylists(
        m_category, before, page.limit,
        [this, before, reply = page.offset == 0 ? firstPage() : nextPage()](
            Outcome<ResultPage<CollectionItem>> outcome) {
            if (outcome.value) {
                const auto next = outcome.value->next;
                m_cursor = next.value_or(0);
                outcome.value->hasMore = next.has_value() && *next != before;
            }
            reply(std::move(outcome));
        });
}

DiscoverArtistsModel::DiscoverArtistsModel(QObject* parent) : DiscoverCardsModel(parent) {}

void DiscoverArtistsModel::setKind(int kind)
{
    if (kind == m_kind)
        return;
    m_kind = kind;
    emit filtersChanged();
    filterChanged();
}

void DiscoverArtistsModel::setArea(int area)
{
    if (area == m_area)
        return;
    m_area = area;
    emit filtersChanged();
    filterChanged();
}

void DiscoverArtistsModel::setInitial(const QString& initial)
{
    if (initial == m_initial)
        return;
    m_initial = initial;
    emit filtersChanged();
    filterChanged();
}

Request DiscoverArtistsModel::loadPage(PageRequest page)
{
    return App::instance()->catalog()->browseArtists(m_kind, m_area, m_initial, page,
                                                     page.offset == 0 ? firstPage() : nextPage());
}

DiscoverAlbumsModel::DiscoverAlbumsModel(QObject* parent) : DiscoverCardsModel(parent) {}

void DiscoverAlbumsModel::setArea(int area)
{
    if (area == m_area)
        return;
    m_area = area;
    emit filtersChanged();
    filterChanged();
}

void DiscoverAlbumsModel::setMode(int mode)
{
    if (mode < Newest || mode > Month || mode == m_mode)
        return;
    m_mode = mode;
    emit filtersChanged();
    filterChanged();
}

Request DiscoverAlbumsModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (m_mode == Newest)
        return catalog->newAlbums(m_area, page, page.offset == 0 ? firstPage() : nextPage());
    // A chart comes whole.
    return catalog->albumChart(
        m_area, m_mode == Month, [reply = firstPage()](Outcome<QList<CollectionItem>> outcome) {
            Outcome<ResultPage<CollectionItem>> page;
            page.partial = outcome.partial;
            page.failure = outcome.failure;
            if (outcome.value)
                page.value = ResultPage<CollectionItem>{*outcome.value, false, {}, {}};
            reply(std::move(page));
        });
}

} // namespace cirrus::ui
