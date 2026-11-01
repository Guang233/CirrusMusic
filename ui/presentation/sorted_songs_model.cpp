#include "sorted_songs_model.h"

namespace cirrus::ui
{

SortedSongsModel::SortedSongsModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(this, signal, this, &SortedSongsModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &SortedSongsModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &SortedSongsModel::countChanged);
}

void SortedSongsModel::setSource(SongListModel* source)
{
    if (source == m_source)
        return;
    m_source = source;
    setSourceModel(source);
    emit sourceChanged();
    emit countChanged();
}

void SortedSongsModel::setFilterText(const QString& text)
{
    if (text == m_filterText)
        return;
    m_filterText = text;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterTextChanged();
    emit countChanged();
}

void SortedSongsModel::setSortField(int field)
{
    if (field == m_field)
        return;
    m_field = field;
    applySort();
}

void SortedSongsModel::setDescending(bool descending)
{
    if (descending == m_descending)
        return;
    m_descending = descending;
    applySort();
}

void SortedSongsModel::cycleSort(int field)
{
    if (field != m_field) {
        m_field = field;
        m_descending = false;
    } else if (!m_descending) {
        m_descending = true;
    } else {
        m_field = Original;
        m_descending = false;
    }
    applySort();
}

void SortedSongsModel::applySort()
{
    // Original order is source order: sort column -1.
    if (m_field == Original)
        sort(-1);
    else
        sort(0, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);
    emit sortChanged();
}

QStringList SortedSongsModel::keys() const
{
    QStringList keys;
    keys.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row)
        keys << keyAt(row);
    return keys;
}

QString SortedSongsModel::keyAt(int row) const
{
    return data(index(row, 0), SongListModel::KeyRole).toString();
}

int SortedSongsModel::rowOf(const QString& key) const
{
    for (int row = 0; row < rowCount(); ++row)
        if (keyAt(row) == key)
            return row;
    return -1;
}

bool SortedSongsModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QString text = m_filterText.trimmed();
    if (text.isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.data(SongListModel::LoadedRole).toBool())
        return false;
    for (const int role : {SongListModel::TitleRole, SongListModel::SecondaryTitleRole,
                           SongListModel::ArtistTextRole, SongListModel::AlbumTitleRole})
        if (index.data(role).toString().contains(text, Qt::CaseInsensitive))
            return true;
    return false;
}

bool SortedSongsModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    // A descending sort places `left` first when lessThan(right, left), so the
    // results below flip with `descending` where the order must not: rows without
    // details go last and ties keep the list's order either way.
    const bool sourceOrder = (left.row() < right.row()) != m_descending;
    const bool leftLoaded = left.data(SongListModel::LoadedRole).toBool();
    const bool rightLoaded = right.data(SongListModel::LoadedRole).toBool();
    if (leftLoaded != rightLoaded)
        return leftLoaded != m_descending;
    if (!leftLoaded)
        return sourceOrder;

    int order = 0;
    switch (m_field) {
    case Title:
        order = m_collator.compare(left.data(SongListModel::TitleRole).toString(),
                                   right.data(SongListModel::TitleRole).toString());
        break;
    case Album:
        order = m_collator.compare(left.data(SongListModel::AlbumTitleRole).toString(),
                                   right.data(SongListModel::AlbumTitleRole).toString());
        break;
    case Duration: {
        const qint64 a = left.data(SongListModel::DurationMsRole).toLongLong();
        const qint64 b = right.data(SongListModel::DurationMsRole).toLongLong();
        // Unknown durations (-1) go last, like rows without details.
        if ((a < 0) != (b < 0))
            return (a >= 0) != m_descending;
        order = a < b ? -1 : a > b ? 1 : 0;
        break;
    }
    default:
        break;
    }
    return order != 0 ? order < 0 : sourceOrder;
}

} // namespace cirrus::ui
