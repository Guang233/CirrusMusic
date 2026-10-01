#include "infrastructure/netease/protocol/protocol_crypto.h"
#include "infrastructure/netease/protocol/xeapi_crypto.h"

#include <QtTest>

#include <deque>

using namespace cirrus::netease;

class NetEaseCryptoTest : public QObject
{
    Q_OBJECT
  private slots:
    void weapiVector();
    void eapiVector();
    void responseDecryption();
    void rejectsInvalidCiphertext();
    void rejectsInvalidInput();
    void randomFailure();
    void boundedRejectionSampling();
    void productionRandom();
    void xeapiVectors_data();
    void xeapiVectors();
    void xeapiSignatureVector();
    void xeapiPublicKeyDecryption();
    void xeapiRejectsInvalidKeys();
    void xeapiRandomFailure();
    void xeapiProductionRandom();
};

// XEAPI vectors: produced by the pinned upstream util/crypto.js (a8c781fd)
// functions with randomness replaced by these fixed role inputs (dev-only
// script, not shipped). The signature and R fields are independently
// cross-checked with the OpenSSL CLI. Synthetic key material, not a live key.
namespace
{
QByteArray seq(int start, int size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>(start + i);
    }
    return bytes;
}

// Hands out the next queued value; a size mismatch fails the draw.
RandomBytes scripted(std::deque<QByteArray> values)
{
    return [values = std::make_shared<std::deque<QByteArray>>(std::move(values))](
               qsizetype size) -> std::optional<QByteArray> {
        if (values->empty() || values->front().size() != size) {
            return std::nullopt;
        }
        auto next = values->front();
        values->pop_front();
        return next;
    };
}

XeapiPublicKey testKey()
{
    return {QStringLiteral("v7"),
            QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="), "sk-test-123"};
}

const QList<FormField> AnonymousBody{{QStringLiteral("username"), QStringLiteral("abc 音乐+/=~*")},
                                     {QStringLiteral("e_r"), QStringLiteral("true")}};
} // namespace

// Synthetic offline vectors: AES/MD5 assembled with OpenSSL CLI; raw RSA
// independently calculated using integer modular exponentiation. No live API.
// Payload UTF-8 bytes: {"msg":"音乐","n":1}; secret: abcdefghijklmnop.
static QJsonObject payload()
{
    return {{QStringLiteral("msg"), QStringLiteral("音乐")}, {QStringLiteral("n"), 1}};
}

void NetEaseCryptoTest::weapiVector()
{
    const auto encoded = encodeWeapi(payload(), [](qsizetype size) -> std::optional<QByteArray> {
        QByteArray bytes(size, Qt::Uninitialized);
        for (qsizetype i = 0; i < size; ++i) {
            bytes[i] = static_cast<char>(i);
        }
        return bytes;
    });
    QVERIFY(encoded.ok());
    QCOMPARE(encoded.fields.size(), 2);
    QCOMPARE(encoded.fields[0].name, QStringLiteral("params"));
    QCOMPARE(encoded.fields[0].value,
             QStringLiteral("HX8eJvhoHqxCzQsuM/AC899Ds1zvwQMIhhZh2jT84kIFo8JfOqs8VAongrVPdq70"));
    QCOMPARE(encoded.fields[1].name, QStringLiteral("encSecKey"));
    QCOMPARE(encoded.fields[1].value,
             QStringLiteral("d15a1683c992095d0c234c19966605c5c5964911268bbeda8cb8d08d834913e59d53"
                            "b32358903a121b5fca784c1f5ae44951fd02524df58ecc98e52cc7cf8689b42c2e93d"
                            "df05b0592512d87f5960467e2f086c018849d76014d323500e30f13ef4cafbb0cf5a"
                            "66731a3f1776c75ca35d0062dac70a3e33245afabcf47938487"));
}

void NetEaseCryptoTest::eapiVector()
{
    const auto encoded = encodeEapi(u"/api/test", payload());
    QVERIFY(encoded.ok());
    QCOMPARE(encoded.fields.size(), 1);
    QCOMPARE(encoded.fields[0].value,
             QStringLiteral("4DC723619A991588865191FD2F319BAD2A98DC3E80C67761B7FE34DE068525ADF"
                            "BB85D41A4535C4C0ADA879574A214FA02C10E7C65B5E42AFF0243DF1457B3392"
                            "4E3F2B9D6E5F47011105E7C6AFC0D04A5BDEF9709569C558A8A6C2EAE3FBFA2"));
}

void NetEaseCryptoTest::responseDecryption()
{
    const auto result = decryptEapi(
        QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09"));
    QVERIFY(result.ok());
    QCOMPARE(result.bytes, QByteArray("{\"code\":200,\"ok\":true}"));
}

void NetEaseCryptoTest::rejectsInvalidCiphertext()
{
    QCOMPARE(decryptEapi({}).error, CryptoError::InvalidInput);
    QCOMPARE(decryptEapi(QByteArray(15, 'x')).error, CryptoError::InvalidInput);
    QCOMPARE(decryptEapi(QByteArray(MaxProtocolPlaintext + 32, 'x')).error,
             CryptoError::InvalidInput);
    const auto truncated = decryptEapi(QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823"));
    QCOMPARE(truncated.error, CryptoError::DecryptionFailure);
    QVERIFY(truncated.bytes.isEmpty());
}

void NetEaseCryptoTest::rejectsInvalidInput()
{
    QCOMPARE(encodeEapi(u"https://evil.example/api/test", payload()).error,
             CryptoError::InvalidInput);
    const QJsonObject large{
        {QStringLiteral("text"), QString(MaxProtocolPlaintext, QLatin1Char('x'))}};
    QCOMPARE(encodeEapi(u"/api/test", large).error, CryptoError::InvalidInput);
    QCOMPARE(encodeWeapi(large).error, CryptoError::InvalidInput);
    QVERIFY(!secureRandomBytes(0));
    QVERIFY(!secureRandomBytes(MaxProtocolPlaintext + 1));
}

void NetEaseCryptoTest::randomFailure()
{
    auto result = encodeWeapi(payload(), [](qsizetype) -> std::optional<QByteArray> { return {}; });
    QCOMPARE(result.error, CryptoError::RandomFailure);
    QVERIFY(result.fields.isEmpty());
    QCOMPARE(encodeWeapi(payload(), {}).error, CryptoError::RandomFailure);
    result = encodeWeapi(payload(), [](qsizetype) { return QByteArray("short"); });
    QCOMPARE(result.error, CryptoError::RandomFailure);
}

void NetEaseCryptoTest::boundedRejectionSampling()
{
    int calls = 0;
    const auto result = encodeWeapi(payload(), [&calls](qsizetype size) {
        ++calls;
        return QByteArray(size, '\xff');
    });
    QCOMPARE(result.error, CryptoError::RandomFailure);
    QCOMPARE(calls, 64);
}

void NetEaseCryptoTest::productionRandom()
{
    const auto first = encodeWeapi(payload());
    const auto second = encodeWeapi(payload());
    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QVERIFY(first.fields[0].value != second.fields[0].value);
    QVERIFY(first.fields[1].value != second.fields[1].value);
}

void NetEaseCryptoTest::xeapiVectors_data()
{
    QTest::addColumn<QList<FormField>>("body");
    QTest::addColumn<bool>("withSession");
    QTest::addColumn<QString>("b");
    QTest::addColumn<QString>("s");
    QTest::addColumn<QString>("r");
    QTest::newRow("fresh-dynamic-key")
        << AnonymousBody << false
        << QStringLiteral(
               "B/"
               "7vdOHVA26QDu4RjpSSkx73EbmfVtCB3xr9iJeyfepPYKpl5Jj5mNb2Tlv3vHJwgByMB54ivA88IklYlWxp"
               "IPJBVNSJokuWdLvLFVZX/"
               "0NVubbgPDX6QKwZ7HXAYEHklOrCwIWp1xrkYSPt7e9BlytIqs8A1NfpIy2PMKUa"
               "kOAxkwNOvBOt+X44HYarLpXUWGNXuRJOhPZR/rz0C79POByFGc4eY0QxUcbQ3CJgKyc=")
        << QStringLiteral("NYBy1jZYgNGu6jKa35EhODhR7SGijjt16WXQ0s0WYlRQUVJTVFVWV1hZWlugUcu2XaI/"
                          "4V8bPyLjEILzl61B"
                          "bK5oj13Jt0yb/sDIoH8TsqzwVmX56M0zMso93SvgimFGUrtCk63+utY=")
        << QStringLiteral("6wpHvI2HYma7VDIHXm3prw==");
    QTest::newRow("session-key")
        << AnonymousBody << true
        << QStringLiteral("ytplCTvf7eJPbfXbYK5OGKWWseqS3l+"
                          "gtwC1D393K2BowG8t3IXYwtLx83ywNVGj8cbKuIXrOJFUXkknMEwu"
                          "z7S2uvS9JV5gsZg1gD5Ybt7gnuzryaOsQ8kazb5hbLxrD6hPco9Z96ZKpDdSONr7nVkB7aTN"
                          "I98rJk6V5m6G"
                          "JSIh5uZZAp1x1IQX4LgiBOq9IRTRGa7fzloCWtCUAmvFQ4UTbIrwQ3eCm7YsIfAS8oA=")
        << QStringLiteral("NYBy1jZYgNGu6jKa35EhODhR7SGijjt16WXQ0s0WYlRQUVJTVFVWV1hZWlusVMuMUa8/"
                          "llMWP1/vFqvWit4j"
                          "T7B4j13Jt0yb/sDIoH8TsqzwVmX56M0zMimgdVF8CywrYZkUxEmT9Ow=")
        << QStringLiteral("oL5euWuLAVMn5tLIXjBTHg==");
    QTest::newRow("empty-body") << QList<FormField>{{QStringLiteral("e_r"), QStringLiteral("true")}}
                                << false
                                << QStringLiteral("B/7vdOHVA26QDu4RjpSSkwgmnpQ1NhlpcjCF2b5/"
                                                  "5m2eUtTVVuIGWbRPvWfULiOhOM9wJsz34LwxIjKvJA4Z"
                                                  "Lqij1bqCiaQQHMT3asW2bX+VT2Ty5Ohunu6C0gIWaEiZ")
                                << QStringLiteral(
                                       "NYBy1jZYgNGu6jKa35EhODhR7SGijjt16WXQ0s0WYlRQUVJTVFVWV1hZWlu"
                                       "gUcu2XaI/4V8bPyLjEILzl61B"
                                       "bK5oj13Jt0yb/sDIoH8TsqzwVmX56M0zMso93SvgimFGUrtCk63+utY=")
                                << QStringLiteral("6wpHvI2HYma7VDIHXm3prw==");
}

void NetEaseCryptoTest::xeapiVectors()
{
    QFETCH(QList<FormField>, body);
    QFETCH(bool, withSession);
    std::deque<QByteArray> draws{seq(0x10, 16), seq(0x20, 32), seq(0x50, 12)};
    std::optional<XeapiSessionKey> session;
    if (withSession) {
        session = XeapiSessionKey{"sid-1", "0123456789abcdef"};
    } else {
        draws.push_front(seq(0x00, 16));
    }
    const auto encoded = encodeXeapi(body, testKey(), session, scripted(std::move(draws)));
    QVERIFY(encoded.ok());
    QCOMPARE(encoded.fields.size(), 3);
    QCOMPARE(encoded.fields[0].name, QStringLiteral("B"));
    QCOMPARE(encoded.fields[1].name, QStringLiteral("S"));
    QCOMPARE(encoded.fields[2].name, QStringLiteral("R"));
    QTEST(encoded.fields[0].value, "b");
    QTEST(encoded.fields[1].value, "s");
    QTEST(encoded.fields[2].value, "r");
}

void NetEaseCryptoTest::xeapiSignatureVector()
{
    QCOMPARE(xeapiSignature("1700000000000", "0123456789012345"),
             QByteArray("Z9T+aFZBr0nWVV5WMzIyYWvtSQUx7ysC9WcxILLfs2c="));
}

void NetEaseCryptoTest::xeapiPublicKeyDecryption()
{
    // AES-256-ECB(static key) of
    // {"version":"v7","publicKey":"eaYx…pRo=","sk":"sk-test-123"}
    const QByteArray encrypted =
        "4+hVmeCQwLTgprLInve9fHSNDGu2fQk0eXSJWn0AQwej4hwIPigJPBD8Dppmf+ffzhWn0FTrxRq0zmGbVkri+jEFM3"
        "GCVoc4xtnf2hjsUYggW4Y4rS5k5i1lpQhXxgpJ";
    const auto key = decryptXeapiPublicKey(encrypted);
    QVERIFY(key);
    QCOMPARE(key->version, testKey().version);
    QCOMPARE(key->publicKey, testKey().publicKey);
    QCOMPARE(key->sk, testKey().sk);

    QVERIFY(!decryptXeapiPublicKey({}));
    QVERIFY(!decryptXeapiPublicKey("not base64!"));
    QVERIFY(!decryptXeapiPublicKey(encrypted.first(64))); // padding fails
    QVERIFY(!decryptXeapiPublicKey(QByteArray(16, 'A').toBase64()));
}

void NetEaseCryptoTest::xeapiRejectsInvalidKeys()
{
    const auto random = [] {
        return scripted({seq(0, 16), seq(0x10, 16), seq(0x20, 32), seq(0x50, 12)});
    };
    auto key = testKey();
    key.publicKey.chop(1);
    QCOMPARE(encodeXeapi({}, key, {}, random()).error, CryptoError::InvalidInput);
    key = testKey();
    key.sk.clear();
    QCOMPARE(encodeXeapi({}, key, {}, random()).error, CryptoError::InvalidInput);
    key = testKey();
    key.version = QStringLiteral("v 7");
    QCOMPARE(encodeXeapi({}, key, {}, random()).error, CryptoError::InvalidInput);
    key = testKey();
    key.sk = "a\nb";
    QCOMPARE(encodeXeapi({}, key, {}, random()).error, CryptoError::InvalidInput);

    // Small-order peer point: the all-zero shared secret must not be used.
    key = testKey();
    key.publicKey = QByteArray(32, '\0');
    const auto lowOrder = encodeXeapi({}, key, {}, random());
    QCOMPARE(lowOrder.error, CryptoError::EncryptionFailure);
    QVERIFY(lowOrder.fields.isEmpty());

    for (const auto& session :
         {XeapiSessionKey{"", "0123456789abcdef"}, XeapiSessionKey{"sid", "short"},
          XeapiSessionKey{"sid", "0123456789abcde f"},
          XeapiSessionKey{"sid", QByteArray(20, 'k')}}) {
        QCOMPARE(encodeXeapi({}, testKey(), session, random()).error, CryptoError::InvalidInput);
    }
    const QList<FormField> large{{QStringLiteral("x"), QString(MaxProtocolPlaintext, u'x')}};
    QCOMPARE(encodeXeapi(large, testKey(), {}, random()).error, CryptoError::InvalidInput);
}

void NetEaseCryptoTest::xeapiRandomFailure()
{
    QCOMPARE(encodeXeapi({}, testKey(), {}, {}).error, CryptoError::RandomFailure);
    // Each role draw failing (queue exhausted at that point) yields no partial output.
    for (int available = 0; available < 4; ++available) {
        std::deque<QByteArray> draws{seq(0, 16), seq(0x10, 16), seq(0x20, 32), seq(0x50, 12)};
        draws.resize(available);
        const auto result = encodeXeapi({}, testKey(), {}, scripted(std::move(draws)));
        QCOMPARE(result.error, CryptoError::RandomFailure);
        QVERIFY(result.fields.isEmpty());
    }
}

void NetEaseCryptoTest::xeapiProductionRandom()
{
    const auto first = encodeXeapi(AnonymousBody, testKey());
    const auto second = encodeXeapi(AnonymousBody, testKey());
    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QVERIFY(first.fields[0].value != second.fields[0].value);
    QVERIFY(first.fields[1].value != second.fields[1].value);
    QCOMPARE(first.fields[2].value, second.fields[2].value); // R has no randomness
}

QTEST_APPLESS_MAIN(NetEaseCryptoTest)
#include "netease_crypto_test.moc"
