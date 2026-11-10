#pragma once

#include "sources.h"

#include <QObject>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class CommentWrites;
class MusicRequests;

// Comments from the backend (docs/ui/integration.md §4 CommentSource, step 9). Reading
// works for guests: the new comment list (recommended, hot, newest) a page at a time,
// and a comment's floor (its replies). Writes need a signed-in account and go through
// CommentWrites. `next` holds the service's paging: "<page>" or "<page>:<before>" for
// the list (newest pages go on from the last comment's time), "<time>" for a floor.
class BackendComments : public QObject, public CommentSource
{
  public:
    explicit BackendComments(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendComments() override;

    Request comments(const CommentTarget& target, comment::Sort sort, const QString& cursor,
                     int limit, Reply<CommentPage> reply) override;
    Request replies(const CommentTarget& target, const QString& commentKey, const QString& cursor,
                    int limit, Reply<CommentPage> reply) override;
    Request setCommentLiked(const CommentTarget& target, const QString& commentKey, bool liked,
                            Reply<bool> reply) override;
    Request publishComment(const CommentTarget& target, const QString& text,
                           Reply<bool> reply) override;
    Request replyToComment(const CommentTarget& target, const QString& commentKey,
                           const QString& text, Reply<bool> reply) override;
    Request deleteComment(const CommentTarget& target, const QString& commentKey,
                          Reply<bool> reply) override;

  private:
    MusicRequests* m_requests;
    CommentWrites* m_writes;
};

} // namespace cirrus::ui
