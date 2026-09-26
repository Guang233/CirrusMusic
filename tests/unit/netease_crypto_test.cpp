#include "infrastructure/netease/protocol/protocol_crypto.h"

#include <QtTest>

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
};

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

QTEST_APPLESS_MAIN(NetEaseCryptoTest)
#include "netease_crypto_test.moc"
