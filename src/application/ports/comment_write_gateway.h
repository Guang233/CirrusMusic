#pragma once
#include "domain/account/session_types.h"
#include "domain/music/comment_write_types.h"
#include <QObject>
namespace cirrus::application
{
class ICommentWriteGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual quint64 submit(domain::SessionToken context, QString accountId,
                           domain::CommentMutation mutation) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::domain::CommentWriteResult result);
};
} // namespace cirrus::application
