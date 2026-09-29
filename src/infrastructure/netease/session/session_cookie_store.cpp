#include "session_cookie_store.h"

#include <QDateTime>
#include <QNetworkCookie>
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

} // namespace cirrus::netease
