#pragma once

#include "domain/account/session_types.h"
#include "domain/music/comment_write_types.h"

#include <QMetaType>

namespace cirrus::application
{

// The existing value-only queued contract. No transport or credentials in this envelope.
struct CommentWriteCommand {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    QString accountId;
    domain::CommentMutation mutation;
};
struct CommentWriteDelivery {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    domain::CommentWriteResult result;
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::CommentWriteCommand)
Q_DECLARE_METATYPE(cirrus::application::CommentWriteDelivery)
