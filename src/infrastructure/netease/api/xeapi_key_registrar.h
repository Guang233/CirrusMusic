#pragma once

#include "infrastructure/netease/network/http_transport.h"
#include "xeapi_key_ring.h"

#include <QPointer>

#include <functional>

namespace cirrus::netease
{

enum class XeapiKeyError {
    None,
    InvalidRequest, // bad device ID or random failure; nothing was sent
    Canceled,
    Transport,
    Decode,
    Rejected,  // business code != 200 or missing fields
    Signature, // response not signed for THIS request's nonce
    InvalidKey,
    RingGone
};

struct XeapiKeyResult {
    XeapiKeyError error = XeapiKeyError::None;
    TransportError transportError{};
    int httpStatus = 0;
    QByteArray retryAfter;
};

// Fetches the installation xeapi public key (register_xeapikey). Single flight,
// no automatic retry; the ring changes only after every check passed. Not bound
// to a login session. Ring must be in the same thread; it is tracked weakly.
class XeapiKeyRegistrar final : public QObject
{
    Q_OBJECT
  public:
    using Clock = std::function<qint64()>; // epoch milliseconds (test seam)
    XeapiKeyRegistrar(XeapiKeyRing& ring, QString deviceId,
                      std::unique_ptr<HttpTransport> transport = {}, Clock clock = {},
                      RandomBytes random = secureRandomBytes, QObject* parent = nullptr);

    [[nodiscard]] bool refresh(); // false if a refresh is already in flight
    [[nodiscard]] bool isRefreshing() const { return m_inFlight; }
    void cancel();

  signals:
    void finished(cirrus::netease::XeapiKeyResult result);

  private:
    void receive(quint64 transportId, const TransportResult& result);
    void complete(XeapiKeyResult result);

    QPointer<XeapiKeyRing> m_ring;
    const QString m_deviceId;
    HttpTransport* m_transport = nullptr; // QObject child
    Clock m_clock;
    RandomBytes m_random;
    bool m_inFlight = false;
    quint64 m_transportId = 0;
    QByteArray m_nonce;
};

} // namespace cirrus::netease

Q_DECLARE_METATYPE(cirrus::netease::XeapiKeyResult)
