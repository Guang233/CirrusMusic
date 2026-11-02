#pragma once

#include "loadable_model.h"
#include "song_item.h"
#include "types.h"

#include <QAbstractListModel>
#include <QDate>
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
// `lead` and `coverKeys` summarize them for the home page.
class DailySongsPageModel : public LoadableModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    // Empty for today; a day from historyDates ("yyyy-MM-dd") for its history.
    Q_PROPERTY(QString date READ date WRITE setDate NOTIFY dateChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    // The first song (empty key when there is none) and the first few covers.
    Q_PROPERTY(cirrus::ui::SongItem lead READ lead NOTIFY summaryChanged)
    Q_PROPERTY(QStringList coverKeys READ coverKeys NOTIFY summaryChanged)
    // Whether any song has a reason (history days have none).
    Q_PROPERTY(bool hasReasons READ hasReasons NOTIFY summaryChanged)
    // "15 首 · 52 分钟"; the length only when every song's is known.
    Q_PROPERTY(QString countText READ countText NOTIFY summaryChanged)
    // Days with history (newest first), after loadHistoryDates().
    Q_PROPERTY(QStringList historyDates READ historyDates NOTIFY historyChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyChanged)

  public:
    explicit DailySongsPageModel(QObject* parent = nullptr);
    [[nodiscard]] QString date() const { return m_date; }
    void setDate(const QString& date);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] SongItem lead() const { return m_lead; }
    [[nodiscard]] QStringList coverKeys() const { return m_coverKeys; }
    [[nodiscard]] bool hasReasons() const { return m_hasReasons; }
    [[nodiscard]] QString countText() const { return m_countText; }
    [[nodiscard]] QStringList historyDates() const { return m_historyDates; }
    [[nodiscard]] bool historyLoading() const { return m_historyRequest.pending(); }

    // Asks for the days with history; `historyChanged`, or `historyFailed` and a
    // `notice`.
    Q_INVOKABLE void loadHistoryDates();
    // Today's songs change at 6:00: reloads when the shown ones are from before.
    Q_INVOKABLE void reloadIfStale();

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void dateChanged();
    void summaryChanged();
    void historyChanged();
    void historyFailed();
    void notice(const QString& message);

  protected:
    Request load() override;
    void clear() override;

  private:
    void summarize(const QList<SongInfo>& songs);
    // The day today's songs belong to: they change at 6:00.
    [[nodiscard]] static QDate recommendationDay();

    QString m_date;
    bool m_complete = false;
    SongListModel* m_songs;
    SongItem m_lead;
    QStringList m_coverKeys;
    bool m_hasReasons = false;
    QString m_countText;
    QDate m_loadedDay;
    QStringList m_historyDates;
    Request m_historyRequest;
};

// The account's latest played songs (`limit` of them); SignedOut for guests.
class RecentSongsModel : public LoadableModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit RecentSongsModel(QObject* parent = nullptr);
    [[nodiscard]] int limit() const { return m_limit; }
    void setLimit(int limit);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void limitChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    int m_limit = 6;
    bool m_complete = false;
    SongListModel* m_songs;
};

// One row of cards (Feed.Shelf); reloads when `shelf` or the account changes.
class ShelfModel : public LoadableModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(int shelf READ shelf WRITE setShelf NOTIFY shelfChanged) // Feed.Shelf
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit ShelfModel(QObject* parent = nullptr);
    [[nodiscard]] int shelf() const { return m_shelf; }
    void setShelf(int shelf);
    [[nodiscard]] CollectionListModel* items() const { return m_items; }

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void shelfChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    int m_shelf = feed::RecommendedPlaylists;
    bool m_complete = false;
    CollectionListModel* m_items;
};

// Charts as cards: key (a playlist), title, updateLabel, coverKey, and previews
// ([{title, subtitle}], the first few songs).
class ChartListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ChartsModel")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        UpdateLabelRole,
        CoverKeyRole,
        PreviewsRole
    };
    Q_ENUM(Role)

    using QAbstractListModel::QAbstractListModel;

    void reset(const QList<ChartInfo>& charts);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();

  private:
    QList<ChartInfo> m_charts;
};

// The charts: those the service previews (the official ones, on the home page and
// first on discover), and the others as cards.
class ChartsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::ChartListModel* charts READ charts CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* others READ others CONSTANT)

  public:
    explicit ChartsModel(QObject* parent = nullptr);
    [[nodiscard]] ChartListModel* charts() const { return m_charts; }
    [[nodiscard]] CollectionListModel* others() const { return m_others; }

  protected:
    Request load() override;
    void clear() override;

  private:
    ChartListModel* m_charts;
    CollectionListModel* m_others;
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
