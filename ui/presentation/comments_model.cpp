#include "comments_model.h"

#include "app.h"
#include "session.h"

#include <QDateTime>

namespace cirrus::ui
{

void CommentListModel::reset(const QList<CommentRow>& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void CommentListModel::insert(int at, const QList<CommentRow>& rows)
{
    if (rows.isEmpty())
        return;
    at = qBound(0, at, int(m_rows.size()));
    beginInsertRows({}, at, at + int(rows.size()) - 1);
    for (qsizetype i = 0; i < rows.size(); ++i)
        m_rows.insert(at + i, rows.at(i));
    endInsertRows();
    emit countChanged();
}

void CommentListModel::remove(int at, int count)
{
    if (count <= 0 || !isValid(at))
        return;
    count = qMin(count, int(m_rows.size()) - at);
    beginRemoveRows({}, at, at + count - 1);
    m_rows.remove(at, count);
    endRemoveRows();
    emit countChanged();
}

void CommentListModel::set(int row, const CommentRow& value)
{
    if (!isValid(row))
        return;
    m_rows[row] = value;
    emit dataChanged(index(row), index(row));
}

int CommentListModel::indexOfComment(const QString& key) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).type == CommentRow::Comment && m_rows.at(i).info.key == key)
            return int(i);
    return -1;
}

int CommentListModel::indexOf(const QString& key) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).type != CommentRow::RepliesEnd && m_rows.at(i).info.key == key)
            return int(i);
    return -1;
}

int CommentListModel::floorSize(int row) const
{
    int size = 0;
    for (qsizetype i = row + 1; i < m_rows.size() && m_rows.at(i).type != CommentRow::Comment; ++i)
        ++size;
    return size;
}

int CommentListModel::commentCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const CommentRow& row) {
        return row.type == CommentRow::Comment && !row.local;
    }));
}

int CommentListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CommentListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !isValid(index.row()))
        return {};
    const CommentRow& row = m_rows.at(index.row());
    const CommentInfo& info = row.info;
    switch (role) {
    case RowTypeRole:
        return int(row.type);
    case KeyRole:
        return info.key;
    case AuthorNameRole:
        return info.authorName;
    case AvatarKeyRole:
        return info.avatarKey;
    case TimeMsRole:
        return info.timeMs;
    case TextRole:
        return info.text.value_or(QString());
    case DeletedRole:
        return !info.text.has_value();
    case HasReplyToRole:
        return info.replyTo.has_value();
    case ReplyToNameRole:
        return info.replyTo ? info.replyTo->authorName : QString();
    case ReplyToTextRole:
        return info.replyTo ? info.replyTo->text : QString();
    case LikedCountRole:
        return info.likedCount;
    case LikedRole:
        return info.liked;
    case ReplyCountRole:
        return info.replyCount;
    case MineRole:
        return row.mine;
    case LocalRole:
        return row.local;
    case ExpandedRole:
        return row.expanded;
    case HasMoreRole:
        return row.hasMore;
    case LoadingRole:
        return row.loading;
    case FailedRole:
        return row.failed;
    default:
        return {};
    }
}

QHash<int, QByteArray> CommentListModel::roleNames() const
{
    return {{RowTypeRole, "rowType"},
            {KeyRole, "key"},
            {AuthorNameRole, "authorName"},
            {AvatarKeyRole, "avatarKey"},
            {TimeMsRole, "timeMs"},
            {TextRole, "text"},
            {DeletedRole, "deleted"},
            {HasReplyToRole, "hasReplyTo"},
            {ReplyToNameRole, "replyToName"},
            {ReplyToTextRole, "replyToText"},
            {LikedCountRole, "likedCount"},
            {LikedRole, "liked"},
            {ReplyCountRole, "replyCount"},
            {MineRole, "mine"},
            {LocalRole, "local"},
            {ExpandedRole, "expanded"},
            {HasMoreRole, "hasMore"},
            {LoadingRole, "loading"},
            {FailedRole, "failed"}};
}

namespace
{
constexpr int kReplyPage = 20;
} // namespace

CommentsPageModel::CommentsPageModel(QObject* parent)
    : PagedModel(parent), m_rows(new CommentListModel(this))
{
    setPageSize(20);
    // Likes and "mine" belong to the account.
    followAccount();
}

void CommentsPageModel::setTarget(int target)
{
    if (target < Song || target > Album || target == m_target.kind)
        return;
    m_target.kind = comment::Target(target);
    emit targetChanged();
    scheduleLoad();
}

void CommentsPageModel::setTargetKey(const QString& key)
{
    if (key == m_target.key)
        return;
    m_target.key = key;
    emit targetChanged();
    scheduleLoad();
}

// Once both target properties are set, whatever order they come in.
void CommentsPageModel::scheduleLoad()
{
    if (m_loadScheduled)
        return;
    m_loadScheduled = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_loadScheduled = false;
            if (m_target.key.isEmpty())
                reset();
            else
                reload();
        },
        Qt::QueuedConnection);
}

void CommentsPageModel::setSort(int sort)
{
    if (sort < Recommended || sort > Newest || sort == m_sort)
        return;
    m_sort = sort;
    emit sortChanged();
    if (state() != loadstate::Idle)
        refresh();
}

void CommentsPageModel::clear()
{
    resetPaging();
    m_next.clear();
    m_floors.clear();
    m_likes.clear();
    m_deletes.clear();
    m_rows->reset({});
}

Request CommentsPageModel::loadPage(PageRequest page)
{
    auto* source = App::instance()->commentSource();
    if (page.offset == 0) {
        return source->comments(m_target, comment::Sort(m_sort), {}, page.limit,
                                handle<CommentPage>([this](CommentPage result) {
                                    // A new list: open floors and pending writes belong to the old
                                    // one.
                                    m_floors.clear();
                                    m_likes.clear();
                                    m_deletes.clear();
                                    m_next = result.next;
                                    m_rows->reset(commentRows(result.items));
                                    setTotal(result.total);
                                    pageArrived(int(result.items.size()),
                                                result.hasMore && !m_next.isEmpty());
                                    return result.items.isEmpty();
                                }));
    }
    return source->comments(m_target, comment::Sort(m_sort), m_next, page.limit,
                            [this, more = handleMore<CommentInfo>([this](QList<CommentInfo> items) {
                                       m_rows->insert(m_rows->rowCount(), commentRows(items));
                                   })](Outcome<CommentPage> outcome) {
                                Outcome<ResultPage<CommentInfo>> paged;
                                paged.failure = outcome.failure;
                                paged.partial = outcome.partial;
                                if (outcome.value) {
                                    m_next = outcome.value->next;
                                    paged.value = ResultPage<CommentInfo>{
                                        outcome.value->items,
                                        outcome.value->hasMore.value_or(true) && !m_next.isEmpty(),
                                        outcome.value->total,
                                        {}};
                                }
                                more(std::move(paged));
                            });
}

QList<CommentRow> CommentsPageModel::commentRows(const QList<CommentInfo>& items) const
{
    QList<CommentRow> rows;
    for (const CommentInfo& info : items)
        rows << rowFor(info, CommentRow::Comment, {});
    return rows;
}

CommentRow CommentsPageModel::rowFor(const CommentInfo& info, CommentRow::Type type,
                                     const QString& parentKey) const
{
    CommentRow row;
    row.type = type;
    row.parentKey = parentKey;
    row.info = info;
    const QString account = App::instance()->session()->accountKey();
    row.mine = !account.isEmpty() && info.authorKey == account;
    return row;
}

// What was just published, as the account wrote it.
CommentInfo CommentsPageModel::localComment(const QString& text) const
{
    const Session* session = App::instance()->session();
    CommentInfo info;
    info.authorKey = session->accountKey();
    info.authorName = session->nickname();
    info.avatarKey = session->avatarKey();
    info.text = text;
    info.timeMs = QDateTime::currentMSecsSinceEpoch();
    info.replyCount = 0;
    return info;
}

void CommentsPageModel::setLiked(int row, bool liked)
{
    if (!m_rows->isValid(row))
        return;
    CommentRow value = m_rows->at(row);
    if (value.type == CommentRow::RepliesEnd || value.local || !value.info.text ||
        value.info.liked == liked)
        return;
    const QString key = value.info.key;
    // Shown at once; undone if the service refuses.
    const auto apply = [this, key](bool to) {
        for (int i = 0; i < m_rows->rowCount(); ++i) {
            CommentRow r = m_rows->at(i);
            if (r.type == CommentRow::RepliesEnd || r.info.key != key || r.info.liked == to)
                continue;
            r.info.liked = to;
            if (r.info.likedCount >= 0)
                r.info.likedCount = qMax<qint64>(0, r.info.likedCount + (to ? 1 : -1));
            m_rows->set(i, r);
        }
    };
    apply(liked);
    m_likes[key] = App::instance()->commentSource()->setCommentLiked(
        m_target, key, liked, [this, key, liked, apply](Outcome<bool> outcome) {
            m_likes[key].release();
            m_likes.erase(key);
            if (outcome.value)
                return;
            apply(!liked);
            emit notice(failureMessage(outcome, liked ? tr("没能点赞，请稍后再试")
                                                      : tr("没能取消点赞，请稍后再试")));
        });
}

void CommentsPageModel::expandReplies(int row)
{
    if (!m_rows->isValid(row) || m_rows->at(row).type != CommentRow::Comment ||
        m_rows->at(row).expanded)
        return;
    CommentRow comment = m_rows->at(row);
    comment.expanded = true;
    m_rows->set(row, comment);
    CommentRow end;
    end.type = CommentRow::RepliesEnd;
    end.parentKey = comment.info.key;
    end.loading = true;
    m_rows->insert(row + 1, {end});
    m_floors[comment.info.key] = Floor{};
    loadReplies(comment.info.key);
}

void CommentsPageModel::moreReplies(int row)
{
    if (!m_rows->isValid(row) || m_rows->at(row).type != CommentRow::RepliesEnd ||
        m_rows->at(row).loading)
        return;
    CommentRow end = m_rows->at(row);
    end.loading = true;
    end.failed = false;
    m_rows->set(row, end);
    loadReplies(end.parentKey);
}

void CommentsPageModel::loadReplies(const QString& commentKey)
{
    Floor& floor = m_floors[commentKey];
    floor.request = App::instance()->commentSource()->replies(
        m_target, commentKey, floor.next, kReplyPage,
        [this, commentKey](Outcome<CommentPage> outcome) {
            const int comment = m_rows->indexOfComment(commentKey);
            if (comment < 0 || !m_floors.contains(commentKey))
                return;
            Floor& floor = m_floors[commentKey];
            floor.request.release();
            const int endRow = comment + m_rows->floorSize(comment);
            CommentRow end = m_rows->at(endRow);
            end.loading = false;
            if (!outcome.value) {
                end.failed = true;
                m_rows->set(endRow, end);
                return;
            }
            floor.next = outcome.value->next;
            // Replies published here are already shown; the service's copies of them
            // are left out (they would show twice).
            QStringList localTexts;
            for (int i = comment + 1; i < endRow; ++i)
                if (m_rows->at(i).local)
                    localTexts << m_rows->at(i).info.text.value_or(QString());
            QList<CommentRow> replies;
            for (const CommentInfo& info : std::as_const(outcome.value->items)) {
                CommentRow reply = rowFor(info, CommentRow::Reply, commentKey);
                if (reply.mine && localTexts.removeOne(info.text.value_or(QString())))
                    continue;
                replies << reply;
            }
            end.failed = false;
            end.hasMore = outcome.value->hasMore.value_or(true) && !floor.next.isEmpty();
            m_rows->set(endRow, end);
            m_rows->insert(endRow, replies);
        });
}

void CommentsPageModel::collapseReplies(int row)
{
    if (!m_rows->isValid(row))
        return;
    int comment = row;
    if (m_rows->at(row).type != CommentRow::Comment)
        comment = m_rows->indexOfComment(m_rows->at(row).parentKey);
    if (comment < 0)
        return;
    CommentRow value = m_rows->at(comment);
    m_floors.erase(value.info.key);
    m_rows->remove(comment + 1, m_rows->floorSize(comment));
    value.expanded = false;
    m_rows->set(comment, value);
}

void CommentsPageModel::publish(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_publishing.pending())
        return;
    m_publishing = App::instance()->commentSource()->publishComment(
        m_target, text, [this, text](Outcome<bool> outcome) {
            m_publishing.release();
            emit publishingChanged();
            if (!outcome.value) {
                emit notice(failureMessage(outcome, tr("没能发表评论，请稍后再试")));
                emit published({}, false);
                return;
            }
            CommentRow row = rowFor(localComment(text), CommentRow::Comment, {});
            row.local = true;
            row.info.key = QStringLiteral("local-%1").arg(++m_localSerial);
            m_rows->insert(0, {row});
            if (total() >= 0)
                setTotal(total() + 1);
            emit notice(tr("已发表"));
            emit published({}, true);
        });
    emit publishingChanged();
}

void CommentsPageModel::reply(int row, const QString& text)
{
    if (!m_rows->isValid(row) || text.trimmed().isEmpty() || m_publishing.pending())
        return;
    const CommentRow target = m_rows->at(row);
    if (target.type == CommentRow::RepliesEnd || target.local)
        return;
    // Replies to a reply go into the same floor, quoting it.
    const QString floorKey = target.type == CommentRow::Reply ? target.parentKey : target.info.key;
    const QString replyTo = target.info.key;
    const CommentExcerpt quoted{target.info.authorName, target.info.text.value_or(QString())};
    const bool quote = target.type == CommentRow::Reply;
    m_publishing = App::instance()->commentSource()->replyToComment(
        m_target, replyTo, text,
        [this, text, floorKey, replyTo, quoted, quote](Outcome<bool> outcome) {
            m_publishing.release();
            emit publishingChanged();
            if (!outcome.value) {
                emit notice(failureMessage(outcome, tr("没能回复，请稍后再试")));
                emit published(replyTo, false);
                return;
            }
            const int comment = m_rows->indexOfComment(floorKey);
            if (comment >= 0) {
                CommentRow parent = m_rows->at(comment);
                if (parent.info.replyCount >= 0)
                    ++parent.info.replyCount;
                m_rows->set(comment, parent);
                CommentRow local = rowFor(localComment(text), CommentRow::Reply, floorKey);
                local.local = true;
                local.info.key = QStringLiteral("local-%1").arg(++m_localSerial);
                if (quote)
                    local.info.replyTo = quoted;
                if (parent.expanded) {
                    m_rows->insert(comment + 1, {local});
                } else {
                    // Open the floor with the new reply on top, the others loading.
                    expandReplies(comment);
                    m_rows->insert(comment + 1, {local});
                }
            }
            emit notice(tr("已回复"));
            emit published(replyTo, true);
        });
    emit publishingChanged();
}

void CommentsPageModel::remove(int row)
{
    if (!m_rows->isValid(row))
        return;
    const CommentRow target = m_rows->at(row);
    if (target.type == CommentRow::RepliesEnd || target.local || !target.mine)
        return;
    const QString key = target.info.key;
    m_deletes[key] = App::instance()->commentSource()->deleteComment(
        m_target, key, [this, key, target](Outcome<bool> outcome) {
            m_deletes[key].release();
            m_deletes.erase(key);
            if (!outcome.value) {
                const bool unknown =
                    outcome.failure && (outcome.failure->kind == failure::Timeout ||
                                        outcome.failure->kind == failure::Unknown);
                emit notice(unknown ? tr("可能没有删除成功，刷新看看")
                                    : failureMessage(outcome, tr("没能删除评论，请稍后再试")));
                return;
            }
            for (int i = 0; i < m_rows->rowCount(); ++i) {
                const CommentRow& r = m_rows->at(i);
                if (r.type == target.type && r.info.key == key) {
                    if (r.type == CommentRow::Comment) {
                        m_floors.erase(key);
                        m_rows->remove(i, 1 + m_rows->floorSize(i));
                        if (total() > 0)
                            setTotal(total() - 1);
                    } else {
                        m_rows->remove(i, 1);
                        const int parent = m_rows->indexOfComment(target.parentKey);
                        if (parent >= 0 && m_rows->at(parent).info.replyCount > 0) {
                            CommentRow p = m_rows->at(parent);
                            --p.info.replyCount;
                            m_rows->set(parent, p);
                        }
                    }
                    break;
                }
            }
            emit notice(tr("已删除"));
        });
}

QString CommentsPageModel::failureMessage(const Outcome<bool>& outcome,
                                          const QString& otherwise) const
{
    if (outcome.failure && outcome.failure->kind == failure::SignedOut)
        return tr("登录后才能参与评论");
    if (outcome.failure &&
        (outcome.failure->kind == failure::Network || outcome.failure->kind == failure::Timeout))
        return tr("网络不可用，%1").arg(otherwise);
    return otherwise;
}

} // namespace cirrus::ui
