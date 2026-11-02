#pragma once

#include "collection.h"
#include "loadable_model.h"
#include "paged_model.h"
#include "types.h"

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// One collection tab of the library page (docs/ui/pages/library.md §2): the account's
// playlists, saved albums, followed artists or subscribed podcasts, a page at a time.
// Playlists can be narrowed to the account's own or saved ones (`filter`); the ones
// whose side is unknown only show under all.
class LibraryCollectionsModel : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use LibraryOverviewModel's tabs")
    Q_PROPERTY(int kind READ kind CONSTANT)                                 // Collection.Kind
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged) // Filter
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    enum Filter { All, Created, Saved };
    Q_ENUM(Filter)

    explicit LibraryCollectionsModel(collection::Kind kind, QObject* parent = nullptr);

    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] int filter() const { return m_filter; }
    void setFilter(int filter);
    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    Q_INVOKABLE void ensureLoaded();

  signals:
    void filterChanged();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override { return int(m_all.size()); }

  private:
    void show();

    collection::Kind m_kind;
    int m_filter = All;
    QList<CollectionItem> m_all;
    CollectionListModel* m_items;
};

// The library page (library.md §2): what the song list cards say, and the four
// collection tabs, each loading when first shown. Guests get SignedOut.
class LibraryOverviewModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    // The last song played, and this week's most played; empty when unknown.
    Q_PROPERTY(QString lastPlayedTitle READ lastPlayedTitle NOTIFY summaryChanged)
    Q_PROPERTY(QString weekTopTitle READ weekTopTitle NOTIFY summaryChanged)
    Q_PROPERTY(cirrus::ui::LibraryCollectionsModel* playlists READ playlists CONSTANT)
    Q_PROPERTY(cirrus::ui::LibraryCollectionsModel* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::LibraryCollectionsModel* artists READ artists CONSTANT)
    Q_PROPERTY(cirrus::ui::LibraryCollectionsModel* podcasts READ podcasts CONSTANT)
    Q_PROPERTY(bool creating READ creating NOTIFY creatingChanged)

  public:
    explicit LibraryOverviewModel(QObject* parent = nullptr);

    [[nodiscard]] QString lastPlayedTitle() const { return m_lastPlayed; }
    [[nodiscard]] QString weekTopTitle() const { return m_weekTop; }
    [[nodiscard]] LibraryCollectionsModel* playlists() const { return m_playlists; }
    [[nodiscard]] LibraryCollectionsModel* albums() const { return m_albums; }
    [[nodiscard]] LibraryCollectionsModel* artists() const { return m_artists; }
    [[nodiscard]] LibraryCollectionsModel* podcasts() const { return m_podcasts; }
    [[nodiscard]] bool creating() const { return m_create.pending(); }

    // Creates an empty playlist (playlistCreated, or a notice on failure).
    Q_INVOKABLE void createPlaylist(const QString& title);

  signals:
    void summaryChanged();
    void creatingChanged();
    void playlistCreated(const QString& key);
    // A short message for the user (a failed creation).
    void notice(const QString& message);
    // The account's playlists changed.
    void libraryChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_lastPlayed;
    QString m_weekTop;
    LibraryCollectionsModel* m_playlists;
    LibraryCollectionsModel* m_albums;
    LibraryCollectionsModel* m_artists;
    LibraryCollectionsModel* m_podcasts;
    Request m_ranking;
    Request m_create;
};

// Recently played playlists, albums or podcasts (one tab of the recent page).
class RecentCollectionsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use RecentPageModel's tabs")
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    RecentCollectionsModel(collection::Kind kind, QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    Q_INVOKABLE void ensureLoaded();

  protected:
    Request load() override;
    void clear() override;

  private:
    collection::Kind m_kind;
    CollectionListModel* m_items;
};

// Recently played podcast voices.
class RecentVoicesModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use RecentPageModel.voices")
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit RecentVoicesModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    Q_INVOKABLE void ensureLoaded();

  protected:
    Request load() override;
    void clear() override;

  private:
    SongListModel* m_songs;
};

// The recent page (library.md §3.2): the service's record of recently played songs
// (at most 100), with playlists, albums, podcasts and voices in their own tabs.
class RecentPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    Q_PROPERTY(QString countText READ countText NOTIFY songsChanged)
    Q_PROPERTY(cirrus::ui::RecentCollectionsModel* playlists READ playlists CONSTANT)
    Q_PROPERTY(cirrus::ui::RecentCollectionsModel* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::RecentCollectionsModel* podcasts READ podcasts CONSTANT)
    Q_PROPERTY(cirrus::ui::RecentVoicesModel* voices READ voices CONSTANT)

  public:
    explicit RecentPageModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] QString countText() const;
    [[nodiscard]] RecentCollectionsModel* playlists() const { return m_playlists; }
    [[nodiscard]] RecentCollectionsModel* albums() const { return m_albums; }
    [[nodiscard]] RecentCollectionsModel* podcasts() const { return m_podcasts; }
    [[nodiscard]] RecentVoicesModel* voices() const { return m_voices; }

  signals:
    void songsChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    SongListModel* m_songs;
    RecentCollectionsModel* m_playlists;
    RecentCollectionsModel* m_albums;
    RecentCollectionsModel* m_podcasts;
    RecentVoicesModel* m_voices;
};

// The listening ranking page (library.md §3.3): most played songs, the last week or
// all time.
class RankingPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    // All time instead of the last week; changing it loads again.
    Q_PROPERTY(bool allTime READ allTime WRITE setAllTime NOTIFY allTimeChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    Q_PROPERTY(QString countText READ countText NOTIFY songsChanged)

  public:
    explicit RankingPageModel(QObject* parent = nullptr);

    [[nodiscard]] bool allTime() const { return m_allTime; }
    void setAllTime(bool allTime);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] QString countText() const;

  signals:
    void allTimeChanged();
    void songsChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    bool m_allTime = false;
    SongListModel* m_songs;
};

} // namespace cirrus::ui
