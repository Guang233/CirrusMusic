#include "application/account/session_coordinator.h"
#include "infrastructure/netease/session/session_cookie_store.h"

#include <QtTest>

using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;

class SessionCookieStoreTest : public QObject
{
    Q_OBJECT
  private slots:
    void hostAndPathScope();
    void parentDomainAndDeletion();
    void invalidOrigins_data();
    void invalidOrigins();
    void invalidHeadersAreAtomic();
    void limitsAreAtomic();
    void headerLimit();
    void lateResponseCannotCrossAccounts();
    void cannotRewindOrReplaceCoordinator();
};

static QUrl origin() { return QUrl(QStringLiteral("https://music.163.com/weapi/login")); }

static SessionToken token() { return {QUuid::createUuid(), 1}; }

void SessionCookieStoreTest::hostAndPathScope()
{
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QCOMPARE(store.apply(context, origin(), {"MUSIC_U=private; Path=/weapi; Secure; HttpOnly"}),
             CookieUpdate::Applied);
    const auto sameHost = store.header(context, origin());
    QVERIFY(sameHost.has_value());
    QCOMPARE(*sameHost, QByteArray("MUSIC_U=private"));
    QCOMPARE(*store.header(context, QUrl(QStringLiteral("https://music.163.com/api/test"))),
             QByteArray{});
    QCOMPARE(*store.header(context, QUrl(QStringLiteral("https://music.163.com/weapiExtra"))),
             QByteArray{});
    QCOMPARE(
        *store.header(context, QUrl(QStringLiteral("https://interface.music.163.com/weapi/test"))),
        QByteArray{});
    QVERIFY(!store.header(context, QUrl(QStringLiteral("http://music.163.com/weapi/test"))));
    QVERIFY(store.advanceTo(context)); // duplicate delivery does not clear cookies
    QCOMPARE(*store.header(context, origin()), QByteArray("MUSIC_U=private"));
}

void SessionCookieStoreTest::parentDomainAndDeletion()
{
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QCOMPARE(
        store.apply(context, origin(), {"MUSIC_U=shared; Domain=.music.163.com; Path=/; Secure"}),
        CookieUpdate::Applied);
    const QUrl sibling(QStringLiteral("https://interfacepc.music.163.com/eapi/test"));
    QCOMPARE(*store.header(context, sibling), QByteArray("MUSIC_U=shared"));
    QVERIFY(!store.header(context, QUrl(QStringLiteral("https://p1.music.126.net/image"))));
    QCOMPARE(store.apply(context, origin(),
                         {"MUSIC_U=deleted; Domain=.music.163.com; Path=/; Max-Age=0; Secure"}),
             CookieUpdate::Applied);
    QCOMPARE(*store.header(context, sibling), QByteArray{});
}

void SessionCookieStoreTest::invalidOrigins_data()
{
    QTest::addColumn<QString>("url");
    const QStringList invalid = {QStringLiteral("http://music.163.com/"),
                                 QStringLiteral("https://music.163.com:8443/"),
                                 QStringLiteral("https://music.163.com.evil.example/"),
                                 QStringLiteral("https://user:pass@music.163.com/"),
                                 QStringLiteral("https://music.163.com/#fragment"),
                                 QStringLiteral("https://163.com/"),
                                 QStringLiteral("https://p1.music.126.net/"),
                                 QStringLiteral("https://wanproxy.127.net/"),
                                 QStringLiteral("file:///tmp/cookie"),
                                 QStringLiteral("https://music.163.com./")};
    for (const auto& url : invalid) {
        QTest::newRow(qPrintable(url)) << url;
    }
}

void SessionCookieStoreTest::invalidOrigins()
{
    QFETCH(QString, url);
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QCOMPARE(store.apply(context, QUrl(url), {"MUSIC_U=secret; Path=/"}),
             CookieUpdate::InvalidOrigin);
    QVERIFY(!store.header(context, QUrl(url)));
}

void SessionCookieStoreTest::invalidHeadersAreAtomic()
{
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QCOMPARE(store.apply(context, origin(), {"MUSIC_U=old; Path=/; Secure"}),
             CookieUpdate::Applied);
    const QList<QByteArray> invalid = {"bad=x; Domain=.163.com",
                                       "bad=x; Domain=evil.example",
                                       "bad=x; Domain=interfacepc.music.163.com",
                                       "bad=x\r\nInjected: yes",
                                       "bad=x\t",
                                       "__Secure-token=x; Path=/",
                                       "__Host-token=x; Secure; Path=/weapi",
                                       "__Host-token=x; Secure; Domain=music.163.com; Path=/",
                                       "",
                                       "not-a-cookie"};
    for (const auto& raw : invalid) {
        QCOMPARE(store.apply(context, origin(), {"MUSIC_U=new; Path=/; Secure", raw}),
                 CookieUpdate::InvalidHeader);
        QCOMPARE(*store.header(context, origin()), QByteArray("MUSIC_U=old"));
    }
    QCOMPARE(store.apply(context, origin(), {"__Host-token=x; Secure; Path=/"}),
             CookieUpdate::Applied);
}

void SessionCookieStoreTest::limitsAreAtomic()
{
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QCOMPARE(store.apply(context, origin(), {"keep=old; Path=/"}), CookieUpdate::Applied);
    QCOMPARE(store.apply(context, origin(), {"keep=new; Path=/", QByteArray(4097, 'x')}),
             CookieUpdate::LimitExceeded);
    QCOMPARE(*store.header(context, origin()), QByteArray("keep=old"));
    QCOMPARE(store.apply(context, origin(), QList<QByteArray>(65, "x=y")),
             CookieUpdate::LimitExceeded);
    // Total-cookie count is bounded across individually valid batches.
    for (int batch = 0; batch < 2; ++batch) {
        QList<QByteArray> headers;
        for (int i = 0; i < 63; ++i) {
            headers.append("c" + QByteArray::number(batch * 63 + i) + "=x; Path=/");
        }
        QCOMPARE(store.apply(context, origin(), headers), CookieUpdate::Applied);
    }
    const auto before = store.header(context, origin());
    QCOMPARE(store.apply(context, origin(), {"extra1=x; Path=/", "extra2=y; Path=/"}),
             CookieUpdate::LimitExceeded);
    QCOMPARE(store.header(context, origin()), before);
}

void SessionCookieStoreTest::headerLimit()
{
    SessionCookieStore store;
    const auto context = token();
    QVERIFY(store.advanceTo(context));
    QList<QByteArray> headers;
    for (int i = 0; i < 5; ++i) {
        headers.append("c" + QByteArray::number(i) + "=" + QByteArray(3500, 'x') + "; Path=/");
    }
    QCOMPARE(store.apply(context, origin(), headers), CookieUpdate::Applied);
    QVERIFY(!store.header(context, origin())); // do not emit >16 KiB Cookie headers
}

void SessionCookieStoreTest::lateResponseCannotCrossAccounts()
{
    SessionCoordinator session;
    SessionCookieStore store;
    QVERIFY(session.authenticate(session.beginAuthentication(), QStringLiteral("A")));
    const auto accountA = session.snapshot().token;
    QVERIFY(store.advanceTo(accountA));
    QCOMPARE(store.apply(accountA, origin(), {"MUSIC_U=A; Path=/; Secure"}), CookieUpdate::Applied);
    session.signOut();
    QVERIFY(store.advanceTo(session.snapshot().token));
    QCOMPARE(*store.header(session.snapshot().token, origin()), QByteArray{});
    QVERIFY(session.authenticate(session.beginAuthentication(), QStringLiteral("B")));
    const auto accountB = session.snapshot().token;
    QVERIFY(store.advanceTo(accountB));
    QCOMPARE(store.apply(accountB, origin(), {"MUSIC_U=B; Path=/; Secure"}), CookieUpdate::Applied);
    QCOMPARE(store.apply(accountA, origin(), {"MUSIC_U=late-A; Path=/; Secure"}),
             CookieUpdate::StaleContext);
    QVERIFY(!store.header(accountA, origin()));
    QCOMPARE(*store.header(accountB, origin()), QByteArray("MUSIC_U=B"));
    QVERIFY(!store.advanceTo(accountA));
    QVERIFY(session.expire(accountB));
    QVERIFY(store.advanceTo(session.snapshot().token));
    QCOMPARE(*store.header(session.snapshot().token, origin()), QByteArray{});
}

void SessionCookieStoreTest::cannotRewindOrReplaceCoordinator()
{
    SessionCookieStore store;
    const auto current = token();
    QVERIFY(!store.advanceTo({}));
    QVERIFY(store.advanceTo(current));
    QVERIFY(!store.advanceTo(token()));
    const SessionToken newer{current.coordinatorId, current.epoch + 1};
    QVERIFY(store.advanceTo(newer));
    QVERIFY(!store.advanceTo(current));
    QCOMPARE(store.apply({}, origin(), {"x=y"}), CookieUpdate::StaleContext);
}

QTEST_GUILESS_MAIN(SessionCookieStoreTest)
#include "session_cookie_store_test.moc"
