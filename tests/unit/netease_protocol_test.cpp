#include "infrastructure/netease/protocol/form_encoding.h"
#include "infrastructure/netease/protocol/official_route.h"

#include <QtTest>

using namespace cirrus::netease;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(cirrus::netease::Protocol)

class NetEaseProtocolTest : public QObject
{
    Q_OBJECT

  private slots:
    void formComponent_data();
    void formComponent();
    void formPreservesOrderAndDuplicateNames();
    void emptyForm();
    void formPreservesLargeIds();
    void officialRoute_data();
    void officialRoute();
    void invalidPath_data();
    void invalidPath();
    void invalidProtocol();
};

void NetEaseProtocolTest::formComponent_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("empty") << QString{} << QByteArray{};
    QTest::newRow("safe-alphabet") << u"AZaz09*-._"_s << QByteArray("AZaz09*-._");
    QTest::newRow("spaces-and-plus") << u"a b+c"_s << QByteArray("a+b%2Bc");
    QTest::newRow("reserved") << u"~!()'/:?&=%#"_s
                              << QByteArray("%7E%21%28%29%27%2F%3A%3F%26%3D%25%23");
    QTest::newRow("already-escaped-is-literal") << u"%2F"_s << QByteArray("%252F");
    QTest::newRow("chinese") << u"音乐"_s << QByteArray("%E9%9F%B3%E4%B9%90");
    QTest::newRow("surrogate-pair") << u"\U0001F3B5"_s << QByteArray("%F0%9F%8E%B5");
    QTest::newRow("controls") << u"\r\n\t"_s << QByteArray("%0D%0A%09");
    QTest::newRow("embedded-null") << QString::fromUtf16(u"a\0b", 3) << QByteArray("a%00b");
    QTest::newRow("lone-high") << QString(QChar(char16_t{0xD800})) << QByteArray("%EF%BF%BD");
    QTest::newRow("lone-low") << QString(QChar(char16_t{0xDC00})) << QByteArray("%EF%BF%BD");
    QTest::newRow("high-before-ascii")
        << (QString(QChar(char16_t{0xD800})) + u"x"_s) << QByteArray("%EF%BF%BDx");
}

void NetEaseProtocolTest::formComponent()
{
    QFETCH(QString, input);
    QFETCH(QByteArray, expected);
    QCOMPARE(encodeFormComponent(input), expected);
}

void NetEaseProtocolTest::formPreservesOrderAndDuplicateNames()
{
    const QList<FormField> fields = {
        {u"b"_s, u"two words"_s}, {u"a"_s, u"+"_s}, {u"b"_s, {}}, {{}, u"&"_s}};
    QCOMPARE(encodeForm(fields), QByteArray("b=two+words&a=%2B&b=&=%26"));
}

void NetEaseProtocolTest::emptyForm()
{
    QCOMPARE(encodeForm({}), QByteArray{});
    QCOMPARE(encodeForm({{{}, {}}}), QByteArray("="));
}

void NetEaseProtocolTest::formPreservesLargeIds()
{
    const QList<FormField> fields = {{u"ids"_s, u"[9007199254740993,9223372036854775807]"_s}};
    QCOMPARE(encodeForm(fields), QByteArray("ids=%5B9007199254740993%2C9223372036854775807%5D"));
}

void NetEaseProtocolTest::officialRoute_data()
{
    QTest::addColumn<Protocol>("protocol");
    QTest::addColumn<QString>("expected");
    QTest::newRow("api") << Protocol::Api << u"https://interface.music.163.com/api/v1/song"_s;
    QTest::newRow("weapi") << Protocol::Weapi << u"https://music.163.com/weapi/v1/song"_s;
    QTest::newRow("eapi") << Protocol::Eapi << u"https://interfacepc.music.163.com/eapi/v1/song"_s;
    QTest::newRow("xeapi") << Protocol::Xeapi
                           << u"https://interface3.music.163.com/xeapi/v1/song"_s;
}

void NetEaseProtocolTest::officialRoute()
{
    QFETCH(Protocol, protocol);
    QFETCH(QString, expected);
    const auto url = officialRequestUrl(protocol, u"/api/v1/song");
    QVERIFY(url.has_value());
    QCOMPARE(url->toString(), expected);
    QVERIFY(url->userInfo().isEmpty());
    QVERIFY(url->query().isEmpty());
    QVERIFY(url->fragment().isEmpty());
    QCOMPARE(url->port(), -1);
}

void NetEaseProtocolTest::invalidPath_data()
{
    QTest::addColumn<QString>("path");
    const QStringList paths = {
        {},
        u"/api"_s,
        u"/api/"_s,
        u"api/song"_s,
        u"/weapi/song"_s,
        u"https://example.org/api/song"_s,
        u"//example.org/api/song"_s,
        u"/api//song"_s,
        u"/api/song/"_s,
        u"/api/../song"_s,
        u"/api/./song"_s,
        u"/api/%2e%2e/song"_s,
        u"/api/song?domain=evil"_s,
        u"/api/song#evil"_s,
        u"/api/\\evil"_s,
        u"/api/歌曲"_s,
        u"/api/song\r\nHeader"_s,
        QString::fromUtf16(u"/api/song\0evil", 14),
    };
    for (qsizetype index = 0; index < paths.size(); ++index) {
        QTest::newRow(qPrintable(QString::number(index))) << paths.at(index);
    }
}

void NetEaseProtocolTest::invalidPath()
{
    QFETCH(QString, path);
    for (Protocol protocol : {Protocol::Api, Protocol::Weapi, Protocol::Eapi, Protocol::Xeapi}) {
        QVERIFY(!officialRequestUrl(protocol, path).has_value());
    }
}

void NetEaseProtocolTest::invalidProtocol()
{
    QVERIFY(!officialRequestUrl(static_cast<Protocol>(99), u"/api/song").has_value());
}

QTEST_APPLESS_MAIN(NetEaseProtocolTest)
#include "netease_protocol_test.moc"
