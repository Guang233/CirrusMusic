#pragma once
#include "fake_http.h"
#include "infrastructure/netease/api/xeapi_key_ring.h"
#include "infrastructure/netease/protocol/openssl_primitives.h"
namespace cirrus::test
{
// Dedicated GET-only issuer fixture, never falls back to a real socket.
class CommentTokenManager : public QNetworkAccessManager
{
  public:
    QList<Plan> plans;
    QList<QNetworkRequest> requests;

  protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoing) override
    {
        Q_ASSERT(op == GetOperation && !outgoing);
        requests.append(request);
        return new FakeReply(
            request, plans.isEmpty() ? Plan{R"(null([200,1,"synthetic-v3"]))"} : plans.takeFirst(),
            this);
    }
};
inline cirrus::netease::XeapiPublicKey commentTestKey()
{
    return {QStringLiteral("v7"),
            QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="), "sk-test-123"};
}
inline QByteArray commentEncrypted(const QByteArray& body)
{
    const auto bytes =
        cirrus::netease::detail::aes(body, "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    return bytes.value_or(QByteArray{});
}
} // namespace cirrus::test
