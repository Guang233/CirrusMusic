#pragma once

#include "paged_model.h"
#include "types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include <unordered_map>

namespace cirrus::ui
{

// One row of the comments page: a comment, one of its replies, or the end of its
// replies (more, collapse, loading or a failure).
struct CommentRow {
    enum Type { Comment, Reply, RepliesEnd };
    Type type = Comment;
    QString parentKey; // replies and their end: the comment they belong to
    CommentInfo info;
    bool mine = false;
    // Published here; the service gave no key, so nothing can be done with it yet.
    bool local = false;
    // Comment: its replies are shown. RepliesEnd: the floor's paging.
    bool expanded = false;
    bool hasMore = false;
    bool loading = false;
    bool failed = false;
};

// The comments page's rows, comments with their shown replies in order
// (docs/ui/pages/comments.md §3).
class CommentListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by CommentsPageModel")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1, // CommentListModel.RowType
        KeyRole,
        AuthorNameRole,
        AvatarKeyRole,
        TimeMsRole,
        TextRole,
        DeletedRole,
        HasReplyToRole,
        ReplyToNameRole,
        ReplyToTextRole, // empty when the original was deleted
        LikedCountRole,  // -1 when unknown
        LikedRole,
        ReplyCountRole, // -1 when unknown
        MineRole,
        LocalRole,
        ExpandedRole,
        HasMoreRole,
        LoadingRole,
        FailedRole,
    };
    Q_ENUM(Role)
    enum RowType {
        Comment = CommentRow::Comment,
        Reply = CommentRow::Reply,
        RepliesEnd = CommentRow::RepliesEnd
    };
    Q_ENUM(RowType)

    using QAbstractListModel::QAbstractListModel;

    void reset(const QList<CommentRow>& rows);
    void insert(int at, const QList<CommentRow>& rows);
    void remove(int at, int count);
    void set(int row, const CommentRow& value);
    [[nodiscard]] const CommentRow& at(int row) const { return m_rows.at(row); }
    [[nodiscard]] bool isValid(int row) const { return row >= 0 && row < m_rows.size(); }
    // The comment row with `key`, or -1.
    [[nodiscard]] int indexOfComment(const QString& key) const;
    // Rows after comment `row` that belong to it (its replies and their end).
    [[nodiscard]] int floorSize(int row) const;
    [[nodiscard]] int commentCount() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void countChanged();

  private:
    QList<CommentRow> m_rows;
};

// The comments of a song, playlist or album (docs/ui/pages/comments.md): sorted and
// paged, replies expanded in place, likes, publishing, replying and deleting. Set
// `target` and `targetKey`; loading starts once both are known.
class CommentsPageModel : public PagedModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int target READ target WRITE setTarget NOTIFY targetChanged) // Target
    Q_PROPERTY(QString targetKey READ targetKey WRITE setTargetKey NOTIFY targetChanged)
    Q_PROPERTY(int sort READ sort WRITE setSort NOTIFY sortChanged) // Sort
    Q_PROPERTY(cirrus::ui::CommentListModel* rows READ rows CONSTANT)
    Q_PROPERTY(bool publishing READ publishing NOTIFY publishingChanged)

  public:
    enum Target { Song = comment::Song, Playlist = comment::Playlist, Album = comment::Album };
    Q_ENUM(Target)
    enum Sort { Recommended = comment::Recommended, Hot = comment::Hot, Newest = comment::Newest };
    Q_ENUM(Sort)

    explicit CommentsPageModel(QObject* parent = nullptr);

    [[nodiscard]] int target() const { return m_target.kind; }
    void setTarget(int target);
    [[nodiscard]] QString targetKey() const { return m_target.key; }
    void setTargetKey(const QString& key);
    [[nodiscard]] int sort() const { return m_sort; }
    void setSort(int sort);
    [[nodiscard]] CommentListModel* rows() const { return m_rows; }
    [[nodiscard]] bool publishing() const { return m_publishing.pending(); }

    // Rows are indexes into `rows`.
    Q_INVOKABLE void setLiked(int row, bool liked);
    Q_INVOKABLE void expandReplies(int row);
    // The next page of a floor (row: its end), or again after a failure.
    Q_INVOKABLE void moreReplies(int row);
    // Row: the comment or its floor's end.
    Q_INVOKABLE void collapseReplies(int row);
    // A new comment, or a reply to comment or reply `row`; `published` follows.
    Q_INVOKABLE void publish(const QString& text);
    Q_INVOKABLE void reply(int row, const QString& text);
    Q_INVOKABLE void remove(int row);

  signals:
    void targetChanged();
    void sortChanged();
    void publishingChanged();
    // `replyTo`: the comment replied to, empty for a new comment.
    void published(const QString& replyTo, bool ok);
    void notice(const QString& message);

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override { return m_rows->commentCount(); }

  private:
    struct Floor {
        QString next;
        Request request;
    };

    void scheduleLoad();
    [[nodiscard]] QList<CommentRow> commentRows(const QList<CommentInfo>& items) const;
    [[nodiscard]] CommentRow rowFor(const CommentInfo& info, CommentRow::Type type,
                                    const QString& parentKey) const;
    [[nodiscard]] CommentInfo localComment(const QString& text) const;
    void loadReplies(const QString& commentKey);
    [[nodiscard]] QString failureMessage(const Outcome<bool>& outcome,
                                         const QString& otherwise) const;

    CommentTarget m_target;
    int m_sort = Recommended;
    bool m_loadScheduled = false;
    CommentListModel* m_rows;
    QString m_next;
    // By comment key; Request is move-only, which QHash doesn't take.
    std::unordered_map<QString, Floor> m_floors;
    std::unordered_map<QString, Request> m_likes;
    std::unordered_map<QString, Request> m_deletes;
    Request m_publishing;
    int m_localSerial = 0;
};

} // namespace cirrus::ui
