#include "collection_list_model.h"

#include "formatter.h"

namespace cirrus::ui
{

void CollectionListModel::reset(const QList<CollectionItem>& items)
{
    const int oldCount = rowCount();
    beginResetModel();
    m_items = items;
    endResetModel();
    if (rowCount() != oldCount)
        emit countChanged();
}

void CollectionListModel::clear() { reset({}); }

void CollectionListModel::append(const QList<CollectionItem>& items)
{
    if (items.isEmpty())
        return;
    const int first = rowCount();
    beginInsertRows({}, first, first + int(items.size()) - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

int CollectionListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant CollectionListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const CollectionItem& item = m_items.at(index.row());
    switch (role) {
    case KeyRole:
        return item.key;
    case KindRole:
        return int(item.kind);
    case TitleRole:
        return item.title;
    case SubtitleRole: {
        QStringList parts;
        switch (item.kind) {
        case collection::Album:
            parts << item.byline;
            if (item.year > 0)
                parts << QString::number(item.year);
            break;
        case collection::Playlist:
            if (item.count >= 0)
                parts << tr("%1 首").arg(Formatter::count(item.count));
            parts << item.byline;
            break;
        case collection::Artist:
            if (item.count >= 0)
                parts << tr("%1 张专辑").arg(Formatter::count(item.count));
            break;
        case collection::Podcast:
            if (item.count >= 0)
                parts << tr("%1 期").arg(Formatter::count(item.count));
            parts << item.byline;
            break;
        }
        parts.removeAll(QString());
        return parts.join(tr(" · "));
    }
    case CoverKeyRole:
        return item.coverKey;
    case CountRole:
        return item.count;
    case PlayCountRole:
        return item.playCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> CollectionListModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {KindRole, "kind"},
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {CoverKeyRole, "coverKey"},
        {CountRole, "count"},
        {PlayCountRole, "playCount"},
    };
}

} // namespace cirrus::ui
