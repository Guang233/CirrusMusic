#include "paged_model.h"

namespace cirrus::ui
{

PagedModel::PagedModel(QObject* parent) : LoadableModel(parent)
{
    connect(this, &LoadableModel::stateChanged, this, &PagedModel::pagingChanged);
}

bool PagedModel::canLoadMore() const
{
    return m_hasMore && !m_more.pending() &&
           (state() == loadstate::Ready || state() == loadstate::Partial);
}

void PagedModel::setPageSize(int size)
{
    size = qMax(1, size);
    if (m_pageSize == size)
        return;
    m_pageSize = size;
    emit pageSizeChanged();
}

void PagedModel::loadMore()
{
    if (!canLoadMore())
        return;
    m_moreError = {};
    m_more = loadPage({loadedCount(), m_pageSize});
    emit pagingChanged();
}

Request PagedModel::loadFirst()
{
    m_more.cancel();
    return loadPage({0, m_pageSize});
}

void PagedModel::pageArrived(int count, std::optional<bool> hasMore)
{
    // Unknown means "maybe" until a page comes back empty.
    m_hasMore = hasMore.value_or(count > 0);
}

void PagedModel::resetPaging()
{
    m_more.cancel();
    m_hasMore = false;
    m_moreError = {};
    emit pagingChanged();
}

} // namespace cirrus::ui
