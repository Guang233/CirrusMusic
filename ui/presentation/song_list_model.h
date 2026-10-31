#pragma once

#include "types.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Songs of any list: playlist, album, search results, daily songs, queue
// (docs/ui/presentation.md §5.3). Rows exist for every key; `loaded` is false while a
// row only has its key and waits for details.
class SongListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a page model")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // App.player's current song is one of the rows.
    Q_PROPERTY(bool containsCurrent READ containsCurrent NOTIFY containsCurrentChanged)

  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        SecondaryTitleRole,
        ArtistsRole, // [{key, name}]
        ArtistTextRole,
        AlbumKeyRole,
        AlbumTitleRole,
        CoverKeyRole,
        DurationMsRole,   // -1 when unknown
        DurationTextRole, // "--:--" when unknown
        LoadedRole,
        UnavailableRole,
        CurrentRole, // the song App.player is on
        ReasonRole,  // recommendation reason, may be empty
    };
    Q_ENUM(Role)

    explicit SongListModel(QObject* parent = nullptr);

    // One row per key; details fill the rows whose keys they match.
    void reset(const QStringList& keys, const QList<SongInfo>& details);
    void clear();
    // More loaded songs at the end (paged lists).
    void append(const QList<SongInfo>& songs);

    // Every key, e.g. for "play all".
    Q_INVOKABLE QStringList keys() const;
    [[nodiscard]] bool containsCurrent() const { return m_containsCurrent; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();
    void containsCurrentChanged();

  private:
    void setCurrentKey(const QString& key);
    void updateContainsCurrent();

    struct Row {
        SongInfo song;
        bool loaded = false;
    };
    QList<Row> m_rows;
    QString m_currentKey;
    bool m_containsCurrent = false;
};

} // namespace cirrus::ui
