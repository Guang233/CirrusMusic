#pragma once

#include "domain/account/session_types.h"

#include <QByteArray>
#include <QList>
#include <QNetworkCookieJar>
#include <QUrl>

#include <optional>

namespace cirrus::netease
{

enum class CookieUpdate { Applied, StaleContext, InvalidOrigin, InvalidHeader, LimitExceeded };

// In-memory, thread-affine credentials owned by one coordinator's current epoch.
// Never attached to QNetworkAccessManager: automatic network cookie IO stays off.
// No disk export and no unrestricted all-cookies accessor.
class SessionCookieStore final
{
  public:
    [[nodiscard]] bool advanceTo(const domain::SessionToken& next);
    [[nodiscard]] CookieUpdate apply(const domain::SessionToken& context, const QUrl& origin,
                                     const QList<QByteArray>& setCookieHeaders);
    // nullopt means stale/invalid origin or >16 KiB; engaged empty means no cookies.
    [[nodiscard]] std::optional<QByteArray> header(const domain::SessionToken& context,
                                                   const QUrl& destination) const;

  private:
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
