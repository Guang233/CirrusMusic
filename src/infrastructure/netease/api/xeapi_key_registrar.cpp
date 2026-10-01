// Protocol adaptation based on api-enhanced module/register_xeapikey.js (MIT).
// See third_party/api-enhanced/NOTICE.md and LICENSE for attribution.
#include "xeapi_key_registrar.h"

#include "infrastructure/netease/protocol/response_decoder.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace cirrus::netease
{
namespace
{

constexpr char LogicalPath[] = "/api/gorilla/anti/crawler/security/key/get";
constexpr char UserAgent[] = "NeteaseMusic/9.5.61.260802021928(9005061);Dalvik/2.1.0 (Linux; U; "
                             "Android 12; HBN-AL00 Build/cd737a2.0)";
constexpr qsizetype ResponseLimit = 64 * 1024;

std::optional<QByteArray> decimalNonce(const RandomBytes& random)
{
    QByteArray nonce;
    // Rejection sampling (bytes >= 250 discarded) keeps digits uniform; bounded.
    for (int attempt = 0; attempt < 16 && nonce.size() < 16; ++attempt) {
        const auto bytes = random ? random(32) : std::nullopt;
        if (!bytes || bytes->size() != 32) {
            return std::nullopt;
        }
        for (const char c : *bytes) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 250 && nonce.size() < 16) {
                nonce.append(static_cast<char>('0' + byte % 10));
            }
        }
    }
    return nonce.size() == 16 ? std::optional<QByteArray>(nonce) : std::nullopt;
}

bool printableAscii(QStringView value, qsizetype limit)
{
    if (value.isEmpty() || value.size() > limit) {
        return false;
    }
    for (const auto c : value) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e) {
            return false;
        }
    }
    return true;
}

// The reference compares String(timestamp); accept a string or a safe integer.
std::optional<QByteArray> timestampText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().toUtf8();
    }
    if (value.isDouble() && value.toDouble() >= 0 && value.toDouble() <= 9007199254740991.0 &&
        value.toDouble() == static_cast<double>(value.toInteger())) {
        return QByteArray::number(value.toInteger());
    }
    return std::nullopt;
}

} // namespace

XeapiKeyRegistrar::XeapiKeyRegistrar(XeapiKeyRing& ring, QString deviceId,
                                     std::unique_ptr<HttpTransport> transport, Clock clock,
                                     RandomBytes random, QObject* parent)
    : QObject(parent), m_ring(&ring), m_deviceId(std::move(deviceId)), m_clock(std::move(clock)),
      m_random(std::move(random))
{
    Q_ASSERT(ring.thread() == thread());
    if (!transport) {
        transport = std::make_unique<HttpTransport>();
    }
    Q_ASSERT(!transport->parent() && transport->thread() == thread());
    transport->setParent(this);
    m_transport = transport.release();
    if (!m_clock) {
        m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    }
    connect(m_transport, &HttpTransport::completed, this, &XeapiKeyRegistrar::receive);
}

bool XeapiKeyRegistrar::refresh()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_inFlight) {
        return false;
    }
    m_inFlight = true;
    const auto nonce = decimalNonce(m_random);
    const auto timestamp = m_clock ? m_clock() : 0;
    const auto current = m_ring ? m_ring->current() : std::nullopt;
    if (!nonce || timestamp <= 0 || !printableAscii(m_deviceId, 128)) {
        // Keep the asynchronous completion contract even for local failures.
        QTimer::singleShot(0, this, [this] { complete({XeapiKeyError::InvalidRequest}); });
        return true;
    }
    m_nonce = *nonce;
    const auto time = QString::number(timestamp);
    const QList<FormField> fields{
        {QStringLiteral("appVersion"), QStringLiteral("9.5.61")},
        {QStringLiteral("currentKeyVersion"), current ? current->version : QString()},
        {QStringLiteral("deviceId"), m_deviceId},
        {QStringLiteral("nonce"), QString::fromLatin1(m_nonce)},
        {QStringLiteral("os"), QStringLiteral("android")},
        {QStringLiteral("requestType"), QStringLiteral("active")},
        {QStringLiteral("signature"),
         QString::fromLatin1(xeapiSignature(time.toLatin1(), m_nonce))},
        {QStringLiteral("t1"), QString()},
        {QStringLiteral("t2"), QString()},
        {QStringLiteral("timestamp"), time},
        {QStringLiteral("uid"), QString()}};
    TransportRequest request;
    request.protocol = Protocol::Api;
    request.logicalPath = QString::fromLatin1(LogicalPath);
    request.formBody = encodeForm(fields);
    request.cookieHeader = "deviceId=" + QUrl::toPercentEncoding(m_deviceId, "!'()*");
    request.userAgent = UserAgent;
    request.responseLimit = ResponseLimit;
    m_transportId = m_transport->post(std::move(request));
    return true;
}

void XeapiKeyRegistrar::cancel()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_transportId != 0) {
        m_transport->cancel(m_transportId); // completes asynchronously as Canceled
    }
}

void XeapiKeyRegistrar::receive(quint64 transportId, const TransportResult& transport)
{
    if (transportId == 0 || transportId != m_transportId) {
        return;
    }
    m_transportId = 0;
    XeapiKeyResult result{XeapiKeyError::None, transport.error, transport.httpStatus,
                          transport.retryAfter};
    if (transport.error == TransportError::Canceled) {
        result.error = XeapiKeyError::Canceled;
        complete(result);
        return;
    }
    if (transport.error != TransportError::None) {
        result.error = XeapiKeyError::Transport;
        complete(result);
        return;
    }
    const auto decoded =
        decodeResponse(transport.body, ResponseEncoding::Json, {ResponseLimit, ResponseLimit});
    if (!decoded.ok()) {
        result.error = XeapiKeyError::Decode;
        complete(result);
        return;
    }
    const auto data = decoded.object.value(QStringLiteral("data")).toObject();
    const auto encrypted = data.value(QStringLiteral("encryptedData"));
    const auto signature = data.value(QStringLiteral("signature"));
    const auto timestamp = timestampText(data.value(QStringLiteral("timestamp")));
    if (decoded.businessCode != 200 || !encrypted.isString() || encrypted.toString().isEmpty() ||
        !signature.isString() || !timestamp) {
        result.error = XeapiKeyError::Rejected;
    } else if (!verifyXeapiSignature(*timestamp, m_nonce, signature.toString().toLatin1())) {
        result.error = XeapiKeyError::Signature;
    } else if (auto key = decryptXeapiPublicKey(encrypted.toString().toLatin1()); !key) {
        result.error = XeapiKeyError::InvalidKey;
    } else if (!m_ring || !m_ring->replace(std::move(*key))) {
        result.error = m_ring ? XeapiKeyError::InvalidKey : XeapiKeyError::RingGone;
    }
    complete(result);
}

void XeapiKeyRegistrar::complete(XeapiKeyResult result)
{
    m_inFlight = false;
    m_nonce.clear();
    emit finished(std::move(result));
}

} // namespace cirrus::netease
