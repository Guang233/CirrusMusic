#pragma once

#include "song_list_model.h"

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// The songs of a SongListModel as a song table shows them: sorted by a column and
// filtered by the in-page find (docs/ui/pages/playlist.md §10). Never changes the
// list itself; "play all" plays keys(), the displayed order.
//   SortedSongsModel { source: pageModel.songs; filterText: find.text }
class SortedSongsModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::SongListModel* source READ source WRITE setSource NOTIFY sourceChanged)
    // Matches title, alias, artists and album, ignoring case; rows still waiting for
    // their details never match.
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int sortField READ sortField WRITE setSortField NOTIFY sortChanged) // Field
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY sortChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    // Original: the list's own order.
    enum Field { Original, Title, Album, Duration };
    Q_ENUM(Field)

    explicit SortedSongsModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* source() const { return m_source; }
    void setSource(SongListModel* source);
    [[nodiscard]] QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);
    [[nodiscard]] int sortField() const { return m_field; }
    void setSortField(int field);
    [[nodiscard]] bool descending() const { return m_descending; }
    void setDescending(bool descending);
    [[nodiscard]] int count() const { return rowCount(); }

    // Ascending, descending, then back to the list's order; another column starts
    // ascending.
    Q_INVOKABLE void cycleSort(int field);
    // Keys in the displayed order.
    Q_INVOKABLE QStringList keys() const;
    Q_INVOKABLE QString keyAt(int row) const;
    // The row of `key`, -1 when filtered out or not in the list.
    Q_INVOKABLE int rowOf(const QString& key) const;

  signals:
    void sourceChanged();
    void filterTextChanged();
    void sortChanged();
    void countChanged();

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

  private:
    void applySort();

    SongListModel* m_source = nullptr;
    QString m_filterText;
    int m_field = Original;
    bool m_descending = false;
    QCollator m_collator;
};

} // namespace cirrus::ui
