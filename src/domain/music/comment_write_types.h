#pragma once
#include "comment_types.h"
#include "logging/log.h"
#include <QMetaType>
#include <QUuid>
namespace cirrus::domain
{
// Plain text, local UTF-16 budget, never normalize/truncate or infer provider limits.
inline bool validCommentWriteText(const QString& text)
{
    if (text.size() > 4096 || !text.isValidUtf16() || text.trimmed().isEmpty())
        return false;
    for (const auto ch : text) {
        const auto n = ch.unicode();
        if (n == 0x7f || (n < 0x20 && n != 9 && n != 10 && n != 13))
            return false;
    }
    return true;
}
inline bool validCommentPostingTarget(const CommentTarget& target)
{
    // New add/reply docs/TS do not establish Podcast container posting support.
    return validCommentTarget(target) &&
           commentResourceKind(target) != CommentResourceKind::Podcast;
}
struct CommentReaction {
    CommentTarget target;
    CommentId comment;
    bool liked = true;
    [[nodiscard]] bool isValid() const
    {
        return validCommentTarget(target) && validMusicId(comment.value);
    }
};
struct CommentPublish {
    CommentTarget target;
    QString content;
    [[nodiscard]] bool isValid() const
    {
        return validCommentPostingTarget(target) && validCommentWriteText(content);
    }
};
struct CommentReply {
    CommentTarget target;
    CommentId parent;
    QString content;
    [[nodiscard]] bool isValid() const
    {
        return validCommentPostingTarget(target) && validMusicId(parent.value) &&
               validCommentWriteText(content);
    }
};
struct CommentDelete {
    CommentTarget target;
    CommentId comment;
    [[nodiscard]] bool isValid() const
    {
        return validCommentTarget(target) && validMusicId(comment.value);
    }
};
using CommentChange = std::variant<CommentReaction, CommentPublish, CommentReply, CommentDelete>;
struct CommentMutation {
    QUuid eventId;
    CommentChange change;
    [[nodiscard]] bool isValid() const
    {
        return !eventId.isNull() && std::visit([](const auto& c) { return c.isValid(); }, change);
    }
};
enum class CommentWriteOutcome { NotSent, Acknowledged, Rejected, Unknown };
CIRRUS_LOG_ENUM(CommentWriteOutcome, NotSent, Acknowledged, Rejected, Unknown)
enum class CommentWriteError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Canceled,
    Busy,
    Network,
    Timeout,
    RateLimited,
    ProtocolFailure,
    InvalidResponse,
    ProviderRejected,
    TokenUnavailable
};
CIRRUS_LOG_ENUM(CommentWriteError, None, InvalidRequest, AuthenticationRequired, StaleContext,
                Canceled, Busy, Network, Timeout, RateLimited, ProtocolFailure, InvalidResponse,
                ProviderRejected, TokenUnavailable)
enum class CommentWriteStep { Unobserved, Token, Writing };
struct CommentWriteResult {
    QUuid eventId;
    CommentWriteOutcome outcome = CommentWriteOutcome::NotSent;
    CommentWriteError error{};
    CommentWriteStep step = CommentWriteStep::Unobserved;
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
    // No created ID/schema proof, raw content, remote URLs, token or inferred ownership.
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::CommentWriteResult)
