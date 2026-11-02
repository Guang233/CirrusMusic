#pragma once

#include "types.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Playlists, albums and artists as cards (docs/ui/presentation.md §5.3).
class CollectionListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a page model")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        KindRole, // Collection.Kind
        TitleRole,
        SubtitleRole, // "Artist · 2024", "12 首 · creator", "3 张专辑"
        CoverKeyRole,
        CountRole,     // -1 when unknown
        PlayCountRole, // playlists, -1 when unknown
    };
    Q_ENUM(Role)

    using QAbstractListModel::QAbstractListModel;

    void reset(const QList<CollectionItem>& items);
    void clear();
    void append(const QList<CollectionItem>& items);
    [[nodiscard]] CollectionItem itemAt(int row) const { return m_items.value(row); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();

  private:
    QList<CollectionItem> m_items;
};

} // namespace cirrus::ui
