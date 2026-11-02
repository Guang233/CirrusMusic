#pragma once

#include "loadable_model.h"
#include "types.h"

namespace cirrus::ui
{

// A LoadableModel whose content arrives in offset pages (docs/ui/presentation.md §4.1):
// the first page through load(), later ones through loadMore(). Failures of later
// pages keep the loaded content and show up in `loadMoreError`.
class PagedModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Base of the paged models")
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY pagingChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY pagingChanged)
    Q_PROPERTY(cirrus::ui::LoadError loadMoreError READ loadMoreError NOTIFY pagingChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    // All results, as the first page told; -1 when unknown.
    Q_PROPERTY(qint64 total READ total NOTIFY pagingChanged)

  public:
    explicit PagedModel(QObject* parent = nullptr);

    [[nodiscard]] bool canLoadMore() const;
    [[nodiscard]] bool loadingMore() const { return m_more.pending(); }
    [[nodiscard]] LoadError loadMoreError() const { return m_moreError; }
    [[nodiscard]] int pageSize() const { return m_pageSize; }
    [[nodiscard]] qint64 total() const { return m_total; }
    void setPageSize(int size);

    // Requests the next page; also retries after a failed one.
    Q_INVOKABLE void loadMore();

  signals:
    void pagingChanged();
    void pageSizeChanged();

  protected:
    // Starts the request for `page` (offset = loaded count).
    virtual Request loadPage(PageRequest page) = 0;
    [[nodiscard]] virtual int loadedCount() const = 0;

    // The first page's request, for load().
    [[nodiscard]] Request loadFirst();
    // Records a page's end: whether more may follow.
    void pageArrived(int count, std::optional<bool> hasMore);
    // Forgets paging state, with clear().
    void resetPaging();
    // The total the first page reported.
    void setTotal(std::optional<qint64> total);

    // Reply adapter for later pages: `append(items)` adds them.
    template <class T, class Append> Reply<ResultPage<T>> handleMore(Append append)
    {
        return [this, append = std::move(append)](Outcome<ResultPage<T>> outcome) {
            m_more.release();
            if (outcome.failure) {
                m_moreError = describe(*outcome.failure);
            } else {
                m_moreError = {};
                const int count = int(outcome.value->items.size());
                append(std::move(outcome.value->items));
                pageArrived(count, outcome.value->hasMore);
            }
            emit pagingChanged();
        };
    }

  private:
    Request m_more;
    bool m_hasMore = false;
    LoadError m_moreError;
    int m_pageSize = 30;
    qint64 m_total = -1;
};

} // namespace cirrus::ui
