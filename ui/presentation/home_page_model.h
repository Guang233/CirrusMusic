#pragma once

#include "loadable_model.h"
#include "types.h"

#include <QAbstractListModel>
#include <QQmlParserStatus>

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// Sections of the home or discover page, each with its own cards or songs.
class FeedSectionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by HomePageModel")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        KindRole = Qt::UserRole + 1, // Feed.SectionKind
        TitleRole,
        CollectionsRole, // CollectionListModel
        SongsRole,       // SongListModel
    };
    Q_ENUM(Role)

    using QAbstractListModel::QAbstractListModel;

    void reset(const QList<FeedSection>& sections);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();

  private:
    struct Row {
        feed::SectionKind kind;
        QString title;
        CollectionListModel* collections;
        SongListModel* songs;
    };
    QList<Row> m_rows;
};

// Home and discover (docs/ui/presentation.md §5.2). Loads once set up, and again when
// `feed` changes.
class HomePageModel : public LoadableModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    Q_PROPERTY(int feed READ feed WRITE setFeed NOTIFY feedChanged) // Feed.Which
    Q_PROPERTY(cirrus::ui::FeedSectionModel* sections READ sections CONSTANT)

  public:
    explicit HomePageModel(QObject* parent = nullptr);

    [[nodiscard]] int feed() const { return m_feed; }
    void setFeed(int feed);
    [[nodiscard]] FeedSectionModel* sections() const { return m_sections; }

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void feedChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    int m_feed = feed::Home;
    bool m_complete = false;
    FeedSectionModel* m_sections;
};

// Today's recommended songs; SignedOut for guests, reloads on account changes.
class DailySongsPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit DailySongsPageModel(QObject* parent = nullptr);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }

  protected:
    Request load() override;
    void clear() override;

  private:
    SongListModel* m_songs;
};

// The account's library (docs/ui/presentation.md §5.2); SignedOut for guests.
class LibraryPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::SongListModel* likedSongs READ likedSongs CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* createdPlaylists READ createdPlaylists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* savedPlaylists READ savedPlaylists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* artists READ artists CONSTANT)
    Q_PROPERTY(cirrus::ui::SongListModel* recentSongs READ recentSongs CONSTANT)

  public:
    explicit LibraryPageModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* likedSongs() const { return m_likedSongs; }
    [[nodiscard]] CollectionListModel* createdPlaylists() const { return m_createdPlaylists; }
    [[nodiscard]] CollectionListModel* savedPlaylists() const { return m_savedPlaylists; }
    [[nodiscard]] CollectionListModel* albums() const { return m_albums; }
    [[nodiscard]] CollectionListModel* artists() const { return m_artists; }
    [[nodiscard]] SongListModel* recentSongs() const { return m_recentSongs; }

  protected:
    Request load() override;
    void clear() override;

  private:
    SongListModel* m_likedSongs;
    CollectionListModel* m_createdPlaylists;
    CollectionListModel* m_savedPlaylists;
    CollectionListModel* m_albums;
    CollectionListModel* m_artists;
    SongListModel* m_recentSongs;
};

} // namespace cirrus::ui
