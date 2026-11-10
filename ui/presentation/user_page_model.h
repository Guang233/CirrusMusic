#pragma once

#include "loadable_model.h"
#include "paged_model.h"
#include "types.h"

#include <QAbstractListModel>

#include <map>

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// Users as rows: a user's follows or fans (docs/ui/pages/user.md §3.3).
class UserListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a page model")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NicknameRole,
        AvatarKeyRole,
        SignatureRole,
        FollowedRole,
    };
    Q_ENUM(Role)

    using QAbstractListModel::QAbstractListModel;

    void reset(const QList<UserItem>& items);
    void append(const QList<UserItem>& items);
    [[nodiscard]] UserItem itemAt(int row) const { return m_items.value(row); }
    // The row of `key`'s user, -1 when not listed.
    [[nodiscard]] int rowOf(const QString& key) const;
    void setFollowed(int row, bool followed);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();

  private:
    QList<UserItem> m_items;
};

// A user's follows (offset pages) or fans (pages by the service's cursor).
class UserPeopleModel : public PagedModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use UserPageModel.follows or .fans")
    Q_PROPERTY(cirrus::ui::UserListModel* items READ items CONSTANT)
    // The user doesn't show this list; the model is then Empty.
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)

  public:
    UserPeopleModel(bool fans, QObject* parent = nullptr);

    [[nodiscard]] UserListModel* items() const { return m_items; }
    [[nodiscard]] bool hidden() const { return m_hidden; }
    void setUserKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();
    // Follows or unfollows the user of `row` (optimistic; reverted with a `notice` on
    // failure).
    Q_INVOKABLE void setFollowed(int row, bool followed);

  signals:
    void hiddenChanged();
    // A short message for the user (a failed follow).
    void notice(const QString& message);

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    void setHidden(bool hidden);

    const bool m_fans;
    QString m_key;
    bool m_hidden = false;
    qint64 m_cursor = 0;
    UserListModel* m_items;
    std::map<QString, Request> m_followRequests; // by user key
};

// The playlists a user made and saved, two lists from one read.
class UserPlaylistsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use UserPageModel.playlists")
    Q_PROPERTY(cirrus::ui::CollectionListModel* created READ created CONSTANT)
    Q_PROPERTY(cirrus::ui::CollectionListModel* saved READ saved CONSTANT)

  public:
    explicit UserPlaylistsModel(QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* created() const { return m_created; }
    [[nodiscard]] CollectionListModel* saved() const { return m_saved; }
    void setUserKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    CollectionListModel* m_created;
    CollectionListModel* m_saved;
};

// A user's listening ranking, the last week or all time.
class UserRankingModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use UserPageModel.ranking")
    // All time instead of the last week; changing it loads again.
    Q_PROPERTY(bool allTime READ allTime WRITE setAllTime NOTIFY allTimeChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    // The user doesn't show the ranking; the model is then Empty.
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)

  public:
    explicit UserRankingModel(QObject* parent = nullptr);

    [[nodiscard]] bool allTime() const { return m_allTime; }
    void setAllTime(bool allTime);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] bool hidden() const { return m_hidden; }
    void setUserKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();

  signals:
    void allTimeChanged();
    void hiddenChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    void setHidden(bool hidden);

    QString m_key;
    bool m_allTime = false;
    bool m_hidden = false;
    SongListModel* m_songs;
};

// User page (docs/ui/pages/user.md): the header; each tab's content has its own model,
// loaded when the tab is first shown. Setting `userKey` loads it.
class UserPageModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString userKey READ userKey WRITE setUserKey NOTIFY userKeyChanged)
    Q_PROPERTY(QString nickname READ nickname NOTIFY headerChanged)
    Q_PROPERTY(QString avatarKey READ avatarKey NOTIFY headerChanged)
    Q_PROPERTY(QString signature READ signature NOTIFY headerChanged)
    Q_PROPERTY(int level READ level NOTIFY headerChanged) // -1 when unknown
    // Counts, -1 when unknown.
    Q_PROPERTY(qint64 followCount READ followCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 fanCount READ fanCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 playlistCount READ playlistCount NOTIFY headerChanged)
    // A musician's artist page; empty for others.
    Q_PROPERTY(QString artistKey READ artistKey NOTIFY headerChanged)
    // The signed-in account's own page.
    Q_PROPERTY(bool own READ own NOTIFY ownChanged)
    // Followed by the account. Setting it follows or unfollows (optimistic; reverted
    // with a `notice` on failure).
    Q_PROPERTY(bool followed READ followed WRITE setFollowed NOTIFY followedChanged)
    Q_PROPERTY(cirrus::ui::UserPlaylistsModel* playlists READ playlists CONSTANT)
    Q_PROPERTY(cirrus::ui::UserRankingModel* ranking READ ranking CONSTANT)
    Q_PROPERTY(cirrus::ui::UserPeopleModel* follows READ follows CONSTANT)
    Q_PROPERTY(cirrus::ui::UserPeopleModel* fans READ fans CONSTANT)

  public:
    explicit UserPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString userKey() const { return m_key; }
    void setUserKey(const QString& key);
    [[nodiscard]] QString nickname() const { return m_info.nickname; }
    [[nodiscard]] QString avatarKey() const { return m_info.avatarKey; }
    [[nodiscard]] QString signature() const { return m_info.signature; }
    [[nodiscard]] int level() const { return m_info.level; }
    [[nodiscard]] qint64 followCount() const { return m_info.follows; }
    [[nodiscard]] qint64 fanCount() const { return m_info.fans; }
    [[nodiscard]] qint64 playlistCount() const { return m_info.playlists; }
    [[nodiscard]] QString artistKey() const { return m_info.artistKey; }
    [[nodiscard]] bool own() const;
    [[nodiscard]] bool followed() const { return m_info.followed; }
    void setFollowed(bool followed);
    [[nodiscard]] UserPlaylistsModel* playlists() const { return m_playlists; }
    [[nodiscard]] UserRankingModel* ranking() const { return m_ranking; }
    [[nodiscard]] UserPeopleModel* follows() const { return m_follows; }
    [[nodiscard]] UserPeopleModel* fans() const { return m_fans; }

  signals:
    void userKeyChanged();
    void headerChanged();
    void ownChanged();
    void followedChanged();
    void notice(const QString& message);

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    UserInfo m_info;
    UserPlaylistsModel* m_playlists;
    UserRankingModel* m_ranking;
    UserPeopleModel* m_follows;
    UserPeopleModel* m_fans;
    Request m_followRequest;
};

} // namespace cirrus::ui
