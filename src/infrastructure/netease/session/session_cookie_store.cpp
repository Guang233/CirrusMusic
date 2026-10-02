#include "session_cookie_store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QSet>
#include <QThread>

namespace cirrus::netease
{
namespace
{

bool approvedHost(const QString& host)
{
    return host == QStringLiteral("music.163.com") ||
           host == QStringLiteral("interface.music.163.com") ||
           host == QStringLiteral("interfacepc.music.163.com") ||
           host == QStringLiteral("interface3.music.163.com");
}

bool approvedOrigin(const QUrl& url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty() &&
           !url.hasFragment() && approvedHost(url.host());
}

bool validCookie(const QNetworkCookie& cookie, const QUrl& origin)
{
    const auto name = cookie.name();
    if (name.isEmpty()) {
        return false;
    }
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              QByteArray("!#$%&'*+-.^_`|~").contains(c))) {
            return false;
        }
    }
    QString domain = cookie.domain().toLower();
    if (domain.startsWith(u'.')) {
        domain.remove(0, 1);
    }
    if (!domain.isEmpty() && (!approvedHost(domain) || !(origin.host() == domain ||
                                                         origin.host().endsWith(u'.' + domain)))) {
        return false;
    }
    if (name.startsWith("__Secure-") && !cookie.isSecure()) {
        return false;
    }
    if (name.startsWith("__Host-") && (!cookie.isSecure() || !cookie.domain().isEmpty() ||
                                       cookie.path() != QStringLiteral("/"))) {
        return false;
    }
    return true;
}

} // namespace

bool SessionCookieStore::accepts(const domain::SessionToken& context) const
{
    Q_ASSERT(QThread::currentThread() == m_jar.thread());
    return context.isValid() && context == m_context;
}

bool SessionCookieStore::advanceTo(const domain::SessionToken& next)
{
    Q_ASSERT(QThread::currentThread() == m_jar.thread());
    if (!next.isValid()) {
        return false;
    }
    if (next == m_context) {
        return true;
    }
    if (m_context.isValid() &&
        (next.coordinatorId != m_context.coordinatorId || next.epoch <= m_context.epoch)) {
        return false;
    }
    m_jar.setAllCookies({});
    m_context = next;
    return true;
}

CookieUpdate SessionCookieStore::apply(const domain::SessionToken& context, const QUrl& origin,
                                       const QList<QByteArray>& headers)
{
    if (!accepts(context)) {
        return CookieUpdate::StaleContext;
    }
    if (!approvedOrigin(origin)) {
        return CookieUpdate::InvalidOrigin;
    }
    if (headers.size() > 64) {
        return CookieUpdate::LimitExceeded;
    }
    QList<QNetworkCookie> incoming;
    qsizetype bytes = 0;
    for (const auto& raw : headers) {
        bytes += raw.size();
        if (raw.size() > 4096 || bytes > 64 * 1024) {
            return CookieUpdate::LimitExceeded;
        }
        for (char c : raw) {
            const auto value = static_cast<unsigned char>(c);
            if (value < 0x20 || value == 0x7f) {
                return CookieUpdate::InvalidHeader;
            }
        }
        const auto parsed = QNetworkCookie::parseCookies(raw);
        if (parsed.isEmpty()) {
            return CookieUpdate::InvalidHeader;
        }
        for (const auto& cookie : parsed) {
            if (!validCookie(cookie, origin)) {
                return CookieUpdate::InvalidHeader;
            }
        }
        incoming.append(parsed);
    }
    // Apply to a temporary jar: invalid/over-limit batches never partially change
    // credentials. Keep normal domain/path/expiry semantics, including deletion.
    Jar candidate;
    candidate.setAllCookies(m_jar.allCookies());
    candidate.setCookiesFromUrl(incoming, origin);
    auto cookies = candidate.allCookies();
    const auto now = QDateTime::currentDateTimeUtc();
    cookies.removeIf([&now](const QNetworkCookie& cookie) {
        return !cookie.isSessionCookie() && cookie.expirationDate() <= now;
    });
    qsizetype storedBytes = 0;
    for (const auto& cookie : cookies) {
        storedBytes += cookie.toRawForm().size();
    }
    if (cookies.size() > 128 || storedBytes > 64 * 1024) {
        return CookieUpdate::LimitExceeded;
    }
    m_jar.setAllCookies(cookies);
    return CookieUpdate::Applied;
}

std::optional<QByteArray> SessionCookieStore::header(const domain::SessionToken& context,
                                                     const QUrl& destination) const
{
    if (!accepts(context) || !approvedOrigin(destination)) {
        return std::nullopt;
    }
    QByteArray result;
    for (const auto& cookie : m_jar.cookiesForUrl(destination)) {
        if (!result.isEmpty()) {
            result.append("; ");
        }
        const auto field = cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
        if (field.size() > 16 * 1024 - result.size()) {
            return std::nullopt;
        }
        result.append(field);
    }
    return result;
}

std::optional<QByteArray> SessionCookieStore::value(const domain::SessionToken& context,
                                                    const QUrl& destination,
                                                    const QByteArray& name) const
{
    if (!accepts(context) || !approvedOrigin(destination) || name.isEmpty()) {
        return std::nullopt;
    }
    std::optional<QByteArray> result;
    for (const auto& cookie : m_jar.cookiesForUrl(destination)) {
        if (cookie.name() == name) {
            if (result && *result != cookie.value()) {
                return std::nullopt;
            }
            result = cookie.value();
        }
    }
    return result.value_or(QByteArray{});
}

bool SessionCookieStore::consistentValue(const domain::SessionToken& context,
                                         const QByteArray& name, const QByteArray& expected) const
{
    if (!accepts(context) || name.isEmpty() || expected.isEmpty()) {
        return false;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto& cookie : m_jar.allCookies()) {
        if (cookie.name() == name && (cookie.isSessionCookie() || cookie.expirationDate() > now) &&
            cookie.value() != expected) {
            return false;
        }
    }
    return true;
}

std::optional<QByteArray> SessionCookieStore::encode(const domain::SessionToken& context) const
{
    if (!accepts(context)) {
        return std::nullopt;
    }
    QJsonArray entries;
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto& cookie : m_jar.allCookies()) {
        if (!cookie.isSessionCookie() && cookie.expirationDate() <= now) {
            continue;
        }
        entries.append(
            QJsonObject{{QStringLiteral("raw"), QString::fromLatin1(cookie.toRawForm().toBase64())},
                        {QStringLiteral("domain"), cookie.domain()}});
    }
    const auto result = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    return result.size() <= 128 * 1024 ? std::optional<QByteArray>(result) : std::nullopt;
}

bool SessionCookieStore::restore(const domain::SessionToken& context, const QByteArray& record)
{
    if (!accepts(context) || record.size() > 128 * 1024) {
        return false;
    }
    const auto doc = QJsonDocument::fromJson(record);
    if (!doc.isArray() || doc.array().size() > 128) {
        return false;
    }
    QList<QNetworkCookie> cookies;
    QSet<QByteArray> identities;
    qsizetype bytes = 0;
    for (const auto& entry : doc.array()) {
        const auto object = entry.toObject();
        const auto rawValue = object.value(QStringLiteral("raw"));
        const auto domainValue = object.value(QStringLiteral("domain"));
        if (object.size() != 2 || !rawValue.isString() || !domainValue.isString()) {
            return false;
        }
        const auto raw = QByteArray::fromBase64Encoding(rawValue.toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
        if (!raw || raw.decoded.size() > 4096 ||
            QString::fromLatin1(raw.decoded.toBase64()) != rawValue.toString()) {
            return false;
        }
        bytes += raw.decoded.size();
        if (bytes > 64 * 1024) {
            return false;
        }
        for (unsigned char c : raw.decoded) {
            if (c < 0x20 || c == 0x7f) {
                return false;
            }
        }
        const auto parsed = QNetworkCookie::parseCookies(raw.decoded);
        if (parsed.size() != 1) {
            return false;
        }
        auto cookie = parsed.first();
        const auto domain = domainValue.toString();
        const auto bareDomain = domain.startsWith(u'.') ? domain.mid(1) : domain;
        const auto parsedDomain =
            cookie.domain().startsWith(u'.') ? cookie.domain().mid(1) : cookie.domain();
        bool exactDomain = false;
        for (const auto& field : raw.decoded.split(';')) {
            exactDomain = exactDomain || field.trimmed().toLower() == "domain=" + domain.toLatin1();
        }
        if (!exactDomain || !approvedHost(bareDomain) || parsedDomain != bareDomain ||
            cookie.path().isEmpty() || !cookie.path().startsWith(u'/')) {
            return false;
        }
        cookie.setDomain(domain); // preserve exact host-only versus Domain scope
        auto validation = cookie;
        if (cookie.name().startsWith("__Host-") && !domain.startsWith(u'.')) {
            // Qt normalizes host-only cookies to an exact host in its jar.
            validation.setDomain({});
        }
        if (!validCookie(validation,
                         QUrl(QStringLiteral("https://") + bareDomain + QStringLiteral("/")))) {
            return false;
        }
        const auto identity =
            cookie.name() + '\0' + domain.toUtf8() + '\0' + cookie.path().toUtf8();
        if (identities.contains(identity)) {
            return false;
        }
        identities.insert(identity);
        if (!cookie.isSessionCookie() &&
            cookie.expirationDate() <= QDateTime::currentDateTimeUtc()) {
            continue;
        }
        cookies.append(cookie);
    }
    m_jar.setAllCookies(cookies); // atomic, after the complete record validates
    return true;
}

std::shared_ptr<SessionCookieStore>
SessionCookieStore::snapshot(const domain::SessionToken& context) const
{
    if (!accepts(context)) {
        return {};
    }
    auto copy = std::make_shared<SessionCookieStore>();
    copy->m_context = context;
    copy->m_jar.setAllCookies(m_jar.allCookies());
    return copy;
}

bool SessionCookieStore::adopt(const SessionCookieStore& snapshot, const domain::SessionToken& from,
                               const domain::SessionToken& to)
{
    if (!accepts(from) || !snapshot.accepts(from) || !to.isValid() ||
        from.coordinatorId != to.coordinatorId || to.epoch <= from.epoch ||
        to.epoch - from.epoch != 1) {
        return false;
    }
    m_jar.setAllCookies(snapshot.m_jar.allCookies());
    m_context = to;
    return true;
}

} // namespace cirrus::netease
