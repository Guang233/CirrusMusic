#include "account_identity_mapper.h"

#include <QVariant>

namespace cirrus::netease
{
namespace
{
QString accountId(const QJsonValue& value)
{
    if (value.isString()) {
        const auto text = value.toString();
        if (text.isEmpty() || text.size() > 19 || text.front() < u'1' || text.front() > u'9') {
            return {};
        }
        for (const auto c : text) {
            if (c < u'0' || c > u'9') {
                return {};
            }
        }
        bool ok = false;
        const auto number = text.toLongLong(&ok);
        return ok && number > 0 ? text : QString{};
    }
    // Qt can normalize floating JSON literals into integer storage after rounding.
    // Accept only the safe numeric range; wider IDs MUST arrive as decimal strings.
    // Never convert an ID through toDouble().
    if (value.toVariant().metaType().id() == QMetaType::LongLong) {
        const auto number = value.toInteger();
        return number > 0 && number <= 9007199254740991LL ? QString::number(number) : QString{};
    }
    return {};
}
} // namespace

std::optional<domain::AccountIdentity> mapAccountIdentity(const QJsonObject& object)
{
    const auto account = object.value(QStringLiteral("account")).toObject();
    const auto profile = object.value(QStringLiteral("profile")).toObject();
    const auto id = accountId(account.value(QStringLiteral("id")));
    const auto userId = accountId(profile.value(QStringLiteral("userId")));
    const auto nickname = profile.value(QStringLiteral("nickname"));
    if (id.isEmpty() || id != userId || !nickname.isString() || nickname.toString().size() > 256 ||
        !nickname.toString().isValidUtf16() || nickname.toString().contains(QChar::Null)) {
        return std::nullopt;
    }
    return domain::AccountIdentity{id, nickname.toString()};
}
} // namespace cirrus::netease
