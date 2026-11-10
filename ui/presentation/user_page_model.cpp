#include "user_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "session.h"
#include "song_list_model.h"

namespace cirrus::ui
{

namespace
{
constexpr int kPeoplePage = 30;
} // namespace

void UserListModel::reset(const QList<UserItem>& items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void UserListModel::append(const QList<UserItem>& items)
{
    if (items.isEmpty())
        return;
    beginInsertRows({}, int(m_items.size()), int(m_items.size() + items.size()) - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

int UserListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant UserListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const UserItem& item = m_items.at(index.row());
    switch (role) {
    case KeyRole:
        return item.key;
    case NicknameRole:
        return item.nickname;
    case AvatarKeyRole:
        return item.avatarKey;
    case SignatureRole:
        return item.signature;
    case FollowedRole:
        return item.followed;
    default:
        return {};
    }
}

QHash<int, QByteArray> UserListModel::roleNames() const
{
    return {{KeyRole, "key"},
            {NicknameRole, "nickname"},
            {AvatarKeyRole, "avatarKey"},
            {SignatureRole, "signature"},
            {FollowedRole, "followed"}};
}

UserPeopleModel::UserPeopleModel(bool fans, QObject* parent)
    : PagedModel(parent), m_fans(fans), m_items(new UserListModel(this))
{
    setPageSize(kPeoplePage);
    followAccount(); // `followed` is the account's
}

void UserPeopleModel::setUserKey(const QString& key)
{
    m_key = key;
    reset();
}

void UserPeopleModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

void UserPeopleModel::setHidden(bool hidden)
{
    if (hidden == m_hidden)
        return;
    m_hidden = hidden;
    emit hiddenChanged();
}

void UserPeopleModel::clear()
{
    resetPaging();
    m_cursor = 0;
    m_items->reset({});
    setHidden(false);
}

int UserPeopleModel::loadedCount() const { return m_items->rowCount(); }

Request UserPeopleModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    const bool first = page.offset == 0;
    Reply<ResultPage<UserItem>> more =
        handleMore<UserItem>([this](QList<UserItem> items) { m_items->append(items); });
    Reply<UserList> reply = [this, first, more = std::move(more)](Outcome<UserList> outcome) {
        if (outcome.value && m_fans) {
            // A cursor that doesn't move on ends the list.
            const auto next = outcome.value->page.next;
            if (!next || *next == m_cursor)
                outcome.value->page.hasMore = false;
            m_cursor = next.value_or(0);
        }
        if (!first) {
            Outcome<ResultPage<UserItem>> page{std::nullopt, outcome.partial, outcome.failure};
            if (outcome.value)
                page.value = std::move(outcome.value->page);
            more(std::move(page));
        }
    };
    if (first)
        reply = [this, inner = std::move(reply), apply = handle<UserList>([this](UserList list) {
                                                     setHidden(list.hidden);
                                                     m_items->reset(list.page.items);
                                                     setTotal(list.page.total);
                                                     pageArrived(int(list.page.items.size()),
                                                                 list.page.hasMore);
                                                     return list.page.items.isEmpty();
                                                 })](Outcome<UserList> outcome) {
            inner(outcome); // the cursor
            apply(std::move(outcome));
        };
    if (m_fans)
        return catalog->userFans(m_key, first ? 0 : m_cursor, page.limit, std::move(reply));
    return catalog->userFollows(m_key, page, std::move(reply));
}

UserPlaylistsModel::UserPlaylistsModel(QObject* parent)
    : LoadableModel(parent), m_created(new CollectionListModel(this)),
      m_saved(new CollectionListModel(this))
{
}

void UserPlaylistsModel::setUserKey(const QString& key)
{
    m_key = key;
    reset();
}

void UserPlaylistsModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

Request UserPlaylistsModel::load()
{
    return App::instance()->catalog()->userPlaylists(
        m_key, handle<QList<CollectionItem>>([this](QList<CollectionItem> items) {
            QList<CollectionItem> created, saved;
            for (const CollectionItem& item : std::as_const(items))
                (item.created.value_or(true) ? created : saved) << item;
            m_created->reset(created);
            m_saved->reset(saved);
            return items.isEmpty();
        }));
}

void UserPlaylistsModel::clear()
{
    m_created->reset({});
    m_saved->reset({});
}

UserRankingModel::UserRankingModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
}

void UserRankingModel::setUserKey(const QString& key)
{
    m_key = key;
    reset();
}

void UserRankingModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

void UserRankingModel::setAllTime(bool allTime)
{
    if (allTime == m_allTime)
        return;
    m_allTime = allTime;
    emit allTimeChanged();
    // The other period replaces the list in place.
    if (state() != loadstate::Idle)
        refresh();
}

void UserRankingModel::setHidden(bool hidden)
{
    if (hidden == m_hidden)
        return;
    m_hidden = hidden;
    emit hiddenChanged();
}

Request UserRankingModel::load()
{
    return App::instance()->catalog()->userRanking(
        m_key, m_allTime, handle<UserRanking>([this](UserRanking ranking) {
            setHidden(ranking.hidden);
            QStringList keys;
            for (const SongInfo& song : std::as_const(ranking.songs))
                keys << song.key;
            m_songs->reset(keys, ranking.songs);
            return ranking.songs.isEmpty();
        }));
}

void UserRankingModel::clear()
{
    m_songs->clear();
    setHidden(false);
}

UserPageModel::UserPageModel(QObject* parent)
    : LoadableModel(parent), m_playlists(new UserPlaylistsModel(this)),
      m_ranking(new UserRankingModel(this)), m_follows(new UserPeopleModel(false, this)),
      m_fans(new UserPeopleModel(true, this))
{
    followAccount(); // `followed` is the account's
    connect(App::instance()->session(), &Session::accountChanged, this, &UserPageModel::ownChanged);
}

void UserPageModel::setUserKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit userKeyChanged();
    emit ownChanged();
    m_playlists->setUserKey(key);
    m_ranking->setUserKey(key);
    m_follows->setUserKey(key);
    m_fans->setUserKey(key);
    if (key.isEmpty())
        reset();
    else
        reload();
}

bool UserPageModel::own() const
{
    return !m_key.isEmpty() && m_key == App::instance()->session()->accountKey();
}

Request UserPageModel::load()
{
    return App::instance()->catalog()->user(m_key, handle<UserInfo>([this](UserInfo info) {
                                                m_info = std::move(info);
                                                emit headerChanged();
                                                return false; // a user always has a page
                                            }));
}

void UserPageModel::clear()
{
    m_info = {};
    emit headerChanged();
}

} // namespace cirrus::ui
