#pragma once

#include "domain/account/session_types.h"

#include <QByteArray>
#include <QList>
#include <QNetworkCookieJar>
#include <QUrl>

#include <memory>
#include <optional>

namespace cirrus::netease
{

enum class CookieUpdate { Applied, StaleContext, InvalidOrigin, InvalidHeader, LimitExceeded };

// In-memory, thread-affine credentials owned by one coordinator's current epoch.
// Never attached to QNetworkAccessManager: automatic network cookie IO stays off.
// No public credential export; the session client alone can encode bounded
// snapshots for the infrastructure's secure store.
class SessionCookieStore final
{
  public:
    [[nodiscard]] bool advanceTo(const domain::SessionToken& next);
    [[nodiscard]] CookieUpdate apply(const domain::SessionToken& context, const QUrl& origin,
                                     const QList<QByteArray>& setCookieHeaders);
    // nullopt means stale/invalid origin or >16 KiB; engaged empty means no cookies.
    [[nodiscard]] std::optional<QByteArray> header(const domain::SessionToken& context,
                                                   const QUrl& destination) const;
    // Scoped lookup for encrypted protocol metadata. Empty = absent; nullopt =
    // invalid/stale or conflicting same-name cookies (never guess a credential).
    [[nodiscard]] std::optional<QByteArray> value(const domain::SessionToken& context,
                                                  const QUrl& destination,
                                                  const QByteArray& name) const;

  private:
    friend class SessionApiClient;
    [[nodiscard]] std::optional<QByteArray> encode(const domain::SessionToken& context) const;
    [[nodiscard]] bool restore(const domain::SessionToken& context, const QByteArray& record);
    [[nodiscard]] bool consistentValue(const domain::SessionToken& context, const QByteArray& name,
                                       const QByteArray& expected) const;
    [[nodiscard]] std::shared_ptr<SessionCookieStore>
    snapshot(const domain::SessionToken& context) const;
    [[nodiscard]] bool adopt(const SessionCookieStore& snapshot, const domain::SessionToken& from,
                             const domain::SessionToken& to);
    class Jar final : public QNetworkCookieJar
    {
      public:
        using QNetworkCookieJar::allCookies;
        using QNetworkCookieJar::setAllCookies;
    };
    [[nodiscard]] bool accepts(const domain::SessionToken& context) const;
    domain::SessionToken m_context;
    Jar m_jar;
};

} // namespace cirrus::netease
