#pragma once

#include "loadable_model.h"
#include "paged_model.h"

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// Song results of a search (one tab of SearchModel), or podcast voices.
class SongResults : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit SongResults(QObject* parent = nullptr, bool voices = false);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    // New query: back to Idle; ensureLoaded() starts it.
    void setQuery(const QString& query);
    void ensureLoaded();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    QString m_query;
    bool m_voices;
    SongListModel* m_songs;
};

// Album, artist or playlist results of a search.
class CollectionResults : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(int kind READ kind CONSTANT) // Collection.Kind
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit CollectionResults(collection::Kind kind, QObject* parent = nullptr);

    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    void setQuery(const QString& query);
    void ensureLoaded();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    collection::Kind m_kind;
    QString m_query;
    CollectionListModel* m_items;
};

// The 综合 tab (docs/ui/pages/search.md §3.1): a few results of each kind from four
// requests at once, and a best match when an artist, album or playlist is named
// exactly as the query. A kind that fails is left out; all failing fails.
class SearchOverview : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* artists READ artists CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* playlists READ playlists CONSTANT)
    // The best match: Collection.Kind, or -1 for none.
    Q_PROPERTY(int bestKind READ bestKind NOTIFY resultsChanged)
    Q_PROPERTY(QString bestKey READ bestKey NOTIFY resultsChanged)
    Q_PROPERTY(QString bestTitle READ bestTitle NOTIFY resultsChanged)
    Q_PROPERTY(QString bestSubtitle READ bestSubtitle NOTIFY resultsChanged)
    Q_PROPERTY(QString bestCoverKey READ bestCoverKey NOTIFY resultsChanged)
    // All results of each kind as the requests told, -1 when unknown:
    // [songs, artists, albums, playlists]. The tabs show them until they load.
    Q_PROPERTY(QList<qint64> totals READ totals NOTIFY resultsChanged)

  public:
    explicit SearchOverview(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] CollectionListModel* artists() const { return m_artists; }
    [[nodiscard]] CollectionListModel* albums() const { return m_albums; }
    [[nodiscard]] CollectionListModel* playlists() const { return m_playlists; }
    [[nodiscard]] int bestKind() const { return m_best ? int(m_best->kind) : -1; }
    [[nodiscard]] QString bestKey() const { return m_best ? m_best->key : QString(); }
    [[nodiscard]] QString bestTitle() const { return m_best ? m_best->title : QString(); }
    [[nodiscard]] QString bestSubtitle() const;
    [[nodiscard]] QString bestCoverKey() const { return m_best ? m_best->coverKey : QString(); }
    [[nodiscard]] QList<qint64> totals() const { return m_totals; }

    void setQuery(const QString& query);
    void ensureLoaded();

  signals:
    void resultsChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    void findBest();

    QString m_query;
    SongListModel* m_songs;
    CollectionListModel* m_artists;
    CollectionListModel* m_albums;
    CollectionListModel* m_playlists;
    std::optional<CollectionItem> m_best;
    QList<qint64> m_totals{-1, -1, -1, -1};
};

} // namespace cirrus::ui
