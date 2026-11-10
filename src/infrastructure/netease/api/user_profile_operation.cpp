#include "user_profile_operation.h"
#include "domain/music/catalog_types.h"

namespace cirrus::netease
{
namespace
{
QString uidOf(const QJsonObject& p)
{
    const auto v = p.value(QStringLiteral("uid"));
    return v.isString() && domain::validMusicId(v.toString()) ? v.toString() : QString();
}
std::optional<domain::CatalogPage> pageOf(const QJsonObject& p)
{
    const auto l = p.value(QStringLiteral("limit")), o = p.value(QStringLiteral("offset"));
    const domain::CatalogPage page{l.toInt(-1), o.toInt(-1)};
    if (!page.isValid() || l != QJsonValue(page.limit) || o != QJsonValue(page.offset))
        return std::nullopt;
    return page;
}
// The fans list pages by time: "0" first, then the last fan's follow time in ms (its
// offset stays 0). Measured 2026-11-10: only the eapi path without the uid honors the
// limit and the cursor; /getfolloweds/{uid} always answers the first 30.
bool validCursor(const QJsonValue& v)
{
    if (!v.isString() || v.toString().isEmpty() || v.toString().size() > 16)
        return false;
    bool ok = false;
    const auto n = v.toString().toLongLong(&ok);
    return ok && n >= 0 && v.toString() == QString::number(n);
}
} // namespace

bool isUserProfileOperation(ApiOperation op)
{
    return op >= ApiOperation::UserDetail && op <= ApiOperation::UserUnfollow;
}

std::optional<ApiOperationContract> userProfileContract(ApiOperation op, const QJsonObject& p)
{
    const auto uid = uidOf(p);
    if (uid.isEmpty())
        return std::nullopt;
    switch (op) {
    case ApiOperation::UserDetail:
        return ApiOperationContract{QStringLiteral("/api/v1/user/detail/") + uid, Protocol::Weapi,
                                    ResponseEncoding::Json, 1024 * 1024};
    case ApiOperation::UserPlaylists:
        return ApiOperationContract{QStringLiteral("/api/user/playlist"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::UserFollows:
        return ApiOperationContract{QStringLiteral("/api/user/getfollows/") + uid, Protocol::Weapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::UserFans:
        return ApiOperationContract{QStringLiteral("/api/user/getfolloweds"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::UserRanking:
        return ApiOperationContract{QStringLiteral("/api/v1/play/record"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::UserFollow:
        return ApiOperationContract{QStringLiteral("/api/user/follow/") + uid, Protocol::Weapi,
                                    ResponseEncoding::Json, 64 * 1024};
    case ApiOperation::UserUnfollow:
        return ApiOperationContract{QStringLiteral("/api/user/delfollow/") + uid, Protocol::Weapi,
                                    ResponseEncoding::Json, 64 * 1024};
    default:
        return std::nullopt;
    }
}

std::optional<QJsonObject> userProfilePayload(ApiOperation op, const QJsonObject& p)
{
    const auto uid = uidOf(p);
    if (!isUserProfileOperation(op) || uid.isEmpty())
        return std::nullopt;
    if (op == ApiOperation::UserDetail || op == ApiOperation::UserFollow ||
        op == ApiOperation::UserUnfollow)
        return p.size() == 1 ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    if (op == ApiOperation::UserRanking) {
        const auto type = p.value(QStringLiteral("type"));
        if (p.size() != 2 || (type != QJsonValue(0) && type != QJsonValue(1)))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("uid"), uid}, {QStringLiteral("type"), type}};
    }
    if (op == ApiOperation::UserFans) {
        const auto limit = p.value(QStringLiteral("limit")), time = p.value(QStringLiteral("time"));
        const auto n = limit.toInt(-1);
        if (p.size() != 3 || n < 1 || n > 100 || limit != QJsonValue(n) || !validCursor(time))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("userId"), uid},
                           {QStringLiteral("time"), time},
                           {QStringLiteral("limit"), n},
                           {QStringLiteral("offset"), 0},
                           {QStringLiteral("getcounts"), QStringLiteral("true")}};
    }
    const auto page = pageOf(p);
    if (!page)
        return std::nullopt;
    if (p.size() != 3)
        return std::nullopt;
    if (op == ApiOperation::UserFollows)
        return QJsonObject{{QStringLiteral("offset"), page->offset},
                           {QStringLiteral("limit"), page->limit},
                           {QStringLiteral("order"), true}};
    return QJsonObject{{QStringLiteral("uid"), uid},
                       {QStringLiteral("limit"), page->limit},
                       {QStringLiteral("offset"), page->offset},
                       {QStringLiteral("includeVideo"), false}};
}
} // namespace cirrus::netease
