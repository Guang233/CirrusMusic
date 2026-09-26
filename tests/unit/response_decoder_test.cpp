#include "infrastructure/netease/protocol/response_decoder.h"

#include <QtTest>

#include <limits>

using namespace cirrus::netease;

class ResponseDecoderTest : public QObject
{
    Q_OBJECT
  private slots:
    void jsonEnvelope();
    void gzipAndEncryptedGzip();
    void corruptedGzip();
    void limits();
    void invalidBusinessCode_data();
    void invalidBusinessCode();
    void malformedJson();
    void noPlaintextFallback();
};

// Synthetic UTF-8 JSON {"code":200,"text":"音乐"}; generated offline with gzip
// and OpenSSL CLI AES-128-ECB. No real server responses or credentials.
static QByteArray gzipFixture()
{
    return QByteArray::fromHex("1f8b0800000000000003ab564ace4f4955b2323230d0512a49ad2851b252"
                               "7a397ff3939d13946a017dc2fcab1c000000");
}

void ResponseDecoderTest::jsonEnvelope()
{
    auto decoded =
        decodeResponse("{\"code\":\"803\",\"uid\":\"9007199254740993\"}", ResponseEncoding::Json);
    QVERIFY(decoded.ok());
    QVERIFY(decoded.businessCode.has_value());
    QCOMPARE(*decoded.businessCode, 803); // decode success != login success
    QCOMPARE(decoded.object.value(QStringLiteral("uid")).toString(),
             QStringLiteral("9007199254740993"));
    decoded = decodeResponse("{\"code\":429}", ResponseEncoding::Json);
    QVERIFY(decoded.ok());
    QCOMPARE(*decoded.businessCode, 429);
    decoded = decodeResponse("{\"code\":0}", ResponseEncoding::Json);
    QVERIFY(decoded.ok());
    QCOMPARE(*decoded.businessCode, 0);
    decoded = decodeResponse("{\"code\":\"-2147483648\"}", ResponseEncoding::Json);
    QVERIFY(decoded.ok());
    QCOMPARE(*decoded.businessCode, std::numeric_limits<int>::min());
    decoded = decodeResponse("{}", ResponseEncoding::Json);
    QVERIFY(decoded.ok());
    QVERIFY(!decoded.businessCode);
}

void ResponseDecoderTest::gzipAndEncryptedGzip()
{
    auto decoded = decodeResponse(gzipFixture(), ResponseEncoding::GzipJson, {1024, 28});
    QVERIFY(decoded.ok());
    QCOMPARE(decoded.object.value(QStringLiteral("text")).toString(), QStringLiteral("音乐"));
    const auto encrypted =
        QByteArray::fromHex("ce31fb563b323757babec700dbc62dbe64114b32ffd56a9aa19bc5ff79b741d788"
                            "ee9157fade83d255550be00633bf6d6aa3b102fbe7296ab0db9ea5c46ad12b");
    for (auto mode : {ResponseEncoding::EapiGzipJson, ResponseEncoding::EapiAutoJson}) {
        decoded = decodeResponse(encrypted, mode);
        QVERIFY(decoded.ok());
        QCOMPARE(*decoded.businessCode, 200);
    }
    const auto encryptedJson =
        QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09");
    for (auto mode : {ResponseEncoding::EapiJson, ResponseEncoding::EapiAutoJson}) {
        decoded = decodeResponse(encryptedJson, mode);
        QVERIFY(decoded.ok());
        QCOMPARE(*decoded.businessCode, 200);
    }
}

void ResponseDecoderTest::corruptedGzip()
{
    auto crc = gzipFixture();
    crc[crc.size() - 8] ^= 1;
    const QList<QByteArray> invalid{crc,
                                    gzipFixture().chopped(3),
                                    gzipFixture() + "extra",
                                    gzipFixture() + gzipFixture(),
                                    "not gzip",
                                    {}};
    for (const auto& bytes : invalid) {
        const auto decoded = decodeResponse(bytes, ResponseEncoding::GzipJson);
        QCOMPARE(decoded.error, DecodeError::InvalidGzip);
        QVERIFY(decoded.object.isEmpty());
    }
}

void ResponseDecoderTest::limits()
{
    QCOMPARE(decodeResponse("{}", ResponseEncoding::Json, {1, 8}).error,
             DecodeError::InputTooLarge);
    QCOMPARE(decodeResponse("{}", ResponseEncoding::Json, {8, 1}).error,
             DecodeError::DecodedTooLarge);
    QCOMPARE(decodeResponse(gzipFixture(), ResponseEncoding::GzipJson, {1024, 27}).error,
             DecodeError::DecodedTooLarge);
    const auto bomb =
        QByteArray::fromHex("1f8b0800000000000003edc1410d00200c04302f27831f62e6600f1282777c2c6d6f"
                            "ba4e676503000000e3e57d7aa2227e0b100000");
    const auto decoded = decodeResponse(bomb, ResponseEncoding::GzipJson, {1024, 64});
    QCOMPARE(decoded.error, DecodeError::DecodedTooLarge);
    QVERIFY(decoded.object.isEmpty());
    for (DecodeLimits limits : {DecodeLimits{0, 32}, {32, -1}, {33 * 1024 * 1024, 32}}) {
        QCOMPARE(decodeResponse("{}", ResponseEncoding::Json, limits).error,
                 DecodeError::InvalidLimits);
    }
}

void ResponseDecoderTest::invalidBusinessCode_data()
{
    QTest::addColumn<QByteArray>("code");
    for (const QByteArray& code :
         QList<QByteArray>{"true", "null", "1.5", "2147483648", "\"2147483648\"", "\" 200\"",
                           "\"+200\"", "\"200.0\"", "\"\"", "{}", "[]"}) {
        QTest::newRow(code.constData()) << code;
    }
}

void ResponseDecoderTest::invalidBusinessCode()
{
    QFETCH(QByteArray, code);
    const auto decoded = decodeResponse("{\"code\":" + code + "}", ResponseEncoding::Json);
    QCOMPARE(decoded.error, DecodeError::InvalidBusinessCode);
    QVERIFY(decoded.object.isEmpty());
    QVERIFY(!decoded.businessCode);
}

void ResponseDecoderTest::malformedJson()
{
    QCOMPARE(decodeResponse("{broken}", ResponseEncoding::Json).error, DecodeError::InvalidJson);
    QCOMPARE(decodeResponse("[]", ResponseEncoding::Json).error, DecodeError::UnexpectedRoot);
    QCOMPARE(decodeResponse("{}garbage", ResponseEncoding::Json).error, DecodeError::InvalidJson);
    QCOMPARE(
        decodeResponse(QByteArray::fromHex("7b2278223a22c0af227d"), ResponseEncoding::Json).error,
        DecodeError::InvalidUtf8);
    QCOMPARE(decodeResponse(QByteArray::fromHex("7b2278223a22e4"), ResponseEncoding::Json).error,
             DecodeError::InvalidUtf8);
}

void ResponseDecoderTest::noPlaintextFallback()
{
    QCOMPARE(decodeResponse("{\"code\":200}", ResponseEncoding::EapiAutoJson).error,
             DecodeError::DecryptionFailed);
    QCOMPARE(decodeResponse("{}", static_cast<ResponseEncoding>(99)).error,
             DecodeError::UnsupportedEncoding);
}

QTEST_APPLESS_MAIN(ResponseDecoderTest)
#include "response_decoder_test.moc"
