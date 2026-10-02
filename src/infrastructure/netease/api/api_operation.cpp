#include "api_operation.h"
#include "domain/music/music_types.h"

#include <QJsonArray>
#include <QSet>

namespace cirrus::netease
{

std::optional<ApiOperationContract> operationContract(ApiOperation operation)
{
    switch (operation) {
    case ApiOperation::QrKey:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/unikey")};
    case ApiOperation::QrCheck:
        return ApiOperationContract{QStringLiteral("/api/login/qrcode/client/login")};
    case ApiOperation::AccountIdentity:
        return ApiOperationContract{QStringLiteral("/api/w/nuser/account/get"), Protocol::Weapi};
    case ApiOperation::SearchSongs:
        return ApiOperationContract{QStringLiteral("/api/search/get"), Protocol::Eapi,
                                    ResponseEncoding::Json, 2 * 1024 * 1024};
    case ApiOperation::SongDetails:
        return ApiOperationContract{QStringLiteral("/api/v3/song/detail"), Protocol::Weapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::SongLyrics:
        return ApiOperationContract{QStringLiteral("/api/song/lyric/v1"), Protocol::Eapi,
                                    ResponseEncoding::Json, 4 * 1024 * 1024};
    case ApiOperation::AnonymousRegister:
        // xeapi responses are always AES-ECB encrypted, gzip detected by magic.
        return ApiOperationContract{QStringLiteral("/api/register/anonimous"), Protocol::Xeapi,
                                    ResponseEncoding::EapiAutoJson};
    }
    return std::nullopt;
}

bool validQrKey(QStringView key)
{
    if (key.isEmpty() || key.size() > 512) {
        return false;
    }
    for (const auto c : key) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

std::optional<QJsonObject> operationPayload(ApiOperation operation, const QJsonObject& parameters)
{
    if (operation == ApiOperation::AccountIdentity ||
        operation == ApiOperation::AnonymousRegister) {
        return parameters.isEmpty() ? std::optional<QJsonObject>(QJsonObject{}) : std::nullopt;
    }
    if (operation == ApiOperation::SearchSongs) {
        const auto limit = parameters.value(QStringLiteral("limit"));
        const auto offset = parameters.value(QStringLiteral("offset"));
        const domain::SongSearch query{parameters.value(QStringLiteral("keywords")).toString(),
                                       limit.toInt(-1), offset.toInt(-1)};
        if (parameters.size() != 3 || !query.isValid() || limit != QJsonValue(query.limit) ||
            offset != QJsonValue(query.offset))
            return std::nullopt;
        return QJsonObject{{QStringLiteral("s"), query.keywords},
                           {QStringLiteral("type"), 1},
                           {QStringLiteral("limit"), query.limit},
                           {QStringLiteral("offset"), query.offset}};
    }
    if (operation == ApiOperation::SongDetails) {
        const auto ids = parameters.value(QStringLiteral("ids")).toArray();
        if (parameters.size() != 1 || ids.isEmpty() || ids.size() > 100)
            return std::nullopt;
        QStringList objects;
        QSet<QString> seen;
        for (const auto value : ids) {
            const auto id = value.toString();
            if (!domain::validMusicId(id) || seen.contains(id))
                return std::nullopt;
            seen.insert(id);
            // c is a JSON string with integer literals; never round a wide ID through double.
            objects.append(QStringLiteral("{\"id\":") + id + u'}');
        }
        return QJsonObject{{QStringLiteral("c"), u'[' + objects.join(u',') + u']'}};
    }
    if (operation == ApiOperation::SongLyrics) {
        const auto id = parameters.value(QStringLiteral("id")).toString();
        if (parameters.size() != 1 || !domain::validMusicId(id))
            return std::nullopt;
        return QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("cp"), false}, {QStringLiteral("tv"), 0},
            {QStringLiteral("lv"), 0},  {QStringLiteral("rv"), 0},     {QStringLiteral("kv"), 0},
            {QStringLiteral("yv"), 0},  {QStringLiteral("ytv"), 0},    {QStringLiteral("yrv"), 0}};
    }
    QJsonObject payload{{QStringLiteral("type"), 3}};
    switch (operation) {
    case ApiOperation::QrKey:
        if (parameters.isEmpty()) {
            return payload;
        }
        break;
    case ApiOperation::QrCheck: {
        const auto key = parameters.value(QStringLiteral("key"));
        if (parameters.size() == 1 && key.isString() && validQrKey(key.toString())) {
            payload.insert(QStringLiteral("key"), key);
            return payload;
        }
        break;
    }
    case ApiOperation::AccountIdentity:
    case ApiOperation::AnonymousRegister:
    case ApiOperation::SearchSongs:
    case ApiOperation::SongDetails:
    case ApiOperation::SongLyrics:
        break;
    }
    return std::nullopt;
}

bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response)
{
    if (!response.ok() || !response.businessCode) {
        return false;
    }
    switch (operation) {
    case ApiOperation::QrKey:
        return *response.businessCode == 200 &&
               validQrKey(response.object.value(QStringLiteral("unikey")).toString());
    case ApiOperation::QrCheck:
        return *response.businessCode >= 801 && *response.businessCode <= 803;
    case ApiOperation::AccountIdentity:
        return *response.businessCode == 200; // staged only in its isolated request snapshot
    case ApiOperation::AnonymousRegister:
    case ApiOperation::SearchSongs:
    case ApiOperation::SongDetails:
    case ApiOperation::SongLyrics:
        return false; // read-only metadata must not mutate verified account credentials
    }
    return false;
}

} // namespace cirrus::netease
