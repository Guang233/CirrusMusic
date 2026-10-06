#include "../support/comment_write_support.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/comment_write_operation.h"
#include "infrastructure/netease/music/comment_write_gateway.h"
#include "infrastructure/netease/protocol/comment_v3_policy.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
CommentMutation mutation(int mode = 0, int kind = 0)
{
    const QString id = "9007199254740993", cid = "9007199254740995";
    CommentTarget target = SongId{id};
    if (kind == 1)
        target = PlaylistId{id};
    else if (kind == 2)
        target = AlbumId{id};
    else if (kind == 3)
        target = ProgramId{id};
    else if (kind == 4)
        target = PodcastId{id};
    CommentChange c = CommentReaction{target, {cid}, mode == 0};
    if (mode == 2)
        c = CommentPublish{target, QStringLiteral("  synthetic \"\\\n文字😀  ")};
    else if (mode == 3)
        c = CommentReply{target, {cid}, QStringLiteral("synthetic reply")};
    else if (mode == 4)
        c = CommentDelete{target, {cid}};
    return {QUuid::createUuid(), c};
}
CommentWriteResult result(const QSignalSpy& spy)
{
    return qvariant_cast<CommentWriteResult>(spy.last()[2]);
}
struct Harness {
    SessionCoordinator session;
    XeapiKeyRing ring;
    FakeManager* http = nullptr;
    CommentTokenManager* tokens = nullptr;
    SessionApiClient client;
    CommentWriteGateway gateway;
    static std::unique_ptr<HttpTransport> transport(FakeManager*& out, QList<Plan> plans)
    {
        auto m = std::make_unique<FakeManager>();
        out = m.get();
        out->plans = plans;
        return std::make_unique<HttpTransport>(std::move(m));
    }
    static std::unique_ptr<CommentV3Issuer> issuer(CommentTokenManager*& out, QList<Plan> plans)
    {
        auto m = std::make_unique<CommentTokenManager>();
        out = m.get();
        out->plans = plans;
        return std::make_unique<CommentV3Issuer>(std::move(m));
    }
    Harness(QList<Plan> plans = {}, QList<Plan> tokenPlans = {}, bool auth = true,
            bool cookie = true, bool key = true)
        : client(transport(http, plans), "synthetic-comment"),
          gateway(session, client, issuer(tokens, tokenPlans))
    {
        client.setXeapiKeyRing(&ring);
        if (key)
            QVERIFY(ring.replace(commentTestKey()));
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        if (!cookie)
            return;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit(int mode = 0, int kind = 0)
    {
        return gateway.submit(session.snapshot().token, "101", mutation(mode, kind));
    }
};
void ready(BackendFacade& f)
{
    f.start();
    BackendEvent e;
    e.kind = BackendEvent::Kind::Started;
    f.deliver(e);
    e.kind = BackendEvent::Kind::Session;
    e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "101"};
    f.deliver(e);
}
} // namespace
class CommentWritesTest : public QObject
{
    Q_OBJECT
  private slots:
    void valuesAndContracts()
    {
        QVERIFY(validCommentWriteText(QStringLiteral(" <b>synthetic</b> \t\r\n😀 ")));
        QVERIFY(validCommentWriteText(QString(4096, 'x')));
        for (const auto& s :
             QStringList{QString{}, " \t\n", QString(4097, 'x'), QString(QChar(0)),
                         QString(QChar(0x7f)), QString(QChar(0xd800)), QString(QChar(1))})
            QVERIFY(!validCommentWriteText(s));
        for (int mode = 0; mode < 5; ++mode)
            for (int kind = 0; kind < 5; ++kind) {
                const auto m = mutation(mode, kind);
                if ((mode == 2 || mode == 3) && kind == 4) {
                    QVERIFY(!m.isValid());
                    continue;
                }
                QVERIFY(m.isValid());
                const auto request = commentWriteRequest(m);
                QVERIFY(request);
                QVERIFY(isCommentWriteOperation(request->operation));
                QCOMPARE(requiresCommentV3(request->operation), mode == 2 || mode == 3);
                const auto c = operationContract(request->operation);
                QVERIFY(c);
                QCOMPARE(c->responseLimit, 64 * 1024);
                QVERIFY(requiresAccountCookie(request->operation));
                QCOMPARE(c->protocol, mode < 2 ? Protocol::Weapi : Protocol::Xeapi);
                QCOMPARE(c->responseEncoding,
                         mode < 2 ? ResponseEncoding::Json : ResponseEncoding::EapiAutoJson);
                const QString paths[]{
                    "/api/v1/comment/like", "/api/v1/comment/unlike", "/api/resource/comments/add",
                    "/api/v1/resource/comments/reply", "/api/resource/comments/delete"};
                QCOMPARE(c->logicalPath, paths[mode]);
                const auto p = operationPayload(request->operation, request->parameters);
                QVERIFY(p);
                const QString prefixes[]{"R_SO_4_", "A_PL_0_", "R_AL_3_", "A_DJ_1_", "A_DR_14_"};
                QCOMPARE(p->value("threadId").toString(), prefixes[kind] + "9007199254740993");
                if (mode != 2)
                    QCOMPARE(p->value("commentId"), QJsonValue("9007199254740995"));
                if (mode == 2 || mode == 3) {
                    QCOMPARE(p->value("resourceType"), QJsonValue("0"));
                    QVERIFY(p->value("content").isString());
                }
                if (mode == 2) {
                    QCOMPARE(p->value("expressionPicId"), QJsonValue("-1"));
                    QCOMPARE(p->value("bubbleId"), QJsonValue("-1"));
                }
                auto wrong = request->parameters;
                wrong["id"] = 1;
                QVERIFY(!operationPayload(request->operation, wrong));
                for (QString extra : {"header", "checkToken", "threadId", "crypto", "uid",
                                      "expressionPicId", "bubbleId", "resourceType", "timestamp"}) {
                    wrong = request->parameters;
                    wrong[extra] = "synthetic";
                    QVERIFY(!operationPayload(request->operation, wrong));
                }
            }
        auto invalid = mutation();
        invalid.eventId = {};
        QVERIFY(!invalid.isValid());
        QVERIFY(!CommentDelete({SongId{"01"}, {"1"}}).isValid());
    }
    void native_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("kind");
        for (int m = 0; m < 5; ++m)
            for (int k = 0; k < 5; ++k)
                if (!((m == 2 || m == 3) && k == 4))
                    QTest::newRow(qPrintable(QString::number(m) + "-" + QString::number(k)))
                        << m << k;
    }
    void native()
    {
        QFETCH(int, mode);
        QFETCH(int, kind);
        const auto response =
            mode < 2 ? QByteArray(R"({"code":200})") : commentEncrypted(R"({"code":200})");
        Plan plan{response};
        plan.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({plan});
        QSignalSpy done(&h.gateway, &ICommentWriteGateway::completed);
        const auto m = mutation(mode, kind);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.gateway.submit(token, "101", m));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CommentWriteOutcome::Acknowledged);
        QCOMPARE(result(done).step, CommentWriteStep::Writing);
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QCOMPARE(h.http->requests.size(), 2);
        const auto request = h.http->requests.last();
        QVERIFY(request.rawHeader("Cookie").contains("synthetic-original"));
        QVERIFY(!request.rawHeader("Cookie").contains("synthetic-rotation"));
        QCOMPARE(h.tokens->requests.size(), (mode == 2 || mode == 3) ? 1 : 0);
        if (mode == 2 || mode == 3) {
            QCOMPARE(request.rawHeader("X-antiCheatToken"), QByteArray("synthetic-v3"));
            const auto issuer = h.tokens->requests.first();
            QCOMPARE(issuer.url().toString(), "https://ac.dun.163yun.com/v3/b?pn=YD00000558929251");
            QVERIFY(issuer.rawHeader("Cookie").isEmpty());
            QVERIFY(issuer.rawHeader("Authorization").isEmpty());
            QVERIFY(issuer.rawHeader("x-music-u").isEmpty());
            QCOMPARE(issuer.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                     int(QNetworkRequest::ManualRedirectPolicy));
        } else
            QVERIFY(request.rawHeader("X-antiCheatToken").isEmpty());
        if (mode < 2) {
            QCOMPARE(request.url().host(), "music.163.com");
            QVERIFY(h.http->bodies.last().startsWith("params="));
        } else {
            QCOMPARE(request.url().host(), "interface3.music.163.com");
            QCOMPARE(request.rawHeader("x-music-u"), QByteArray("synthetic-original"));
            QVERIFY(!h.http->bodies.last().contains("synthetic reply"));
        }
        QVERIFY(h.client.saveCredentials(token, "101"));
    }
    void badToken_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        const QList<QByteArray> bodies{"",
                                       "{}",
                                       "null([])",
                                       "other([200,1,\"synthetic\"])",
                                       "null([403,1,\"synthetic\"])",
                                       "null([200,1,\"\"])",
                                       "null([200,1,\"x\\ny\"])",
                                       "null([200,1.5,\"synthetic\"])",
                                       "null([200,9007199254740992,\"synthetic\"])",
                                       "null([200,1,42])",
                                       "null([200,1,\"é\"])",
                                       "null([200,1,\"synthetic\",0])",
                                       "null([200,1,\"synthetic\"]);evil()",
                                       QByteArray(16385, 'x'),
                                       "null([\"200\",1,\"synthetic\"])"};
        int n = 0;
        for (const auto& b : bodies)
            QTest::newRow(qPrintable(QString::number(++n))) << b << 200;
        QTest::newRow("redirect") << QByteArray(R"(null([200,1,"synthetic"]))") << 302;
        QTest::newRow("http429") << QByteArray(R"(null([200,1,"synthetic"]))") << 429;
    }
    void badToken()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        Harness h({}, {Plan{body, status}});
        QSignalSpy done(&h.gateway, &ICommentWriteGateway::completed);
        QVERIFY(h.submit(2));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CommentWriteOutcome::NotSent);
        QCOMPARE(result(done).error, CommentWriteError::TokenUnavailable);
        QCOMPARE(result(done).step, CommentWriteStep::Token);
        QCOMPARE(h.http->requests.size(), 1);
        QCOMPARE(h.tokens->requests.size(), 1);
    }
    void responses_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CommentWriteOutcome>("outcome");
        for (int m = 0; m < 5; ++m) {
            int row = 0;
            for (const auto& b :
                 QList<QByteArray>{R"({"code":200})", R"({"code":"200"})", R"({"code":403})",
                                   R"({"code":512})", R"({"code":-462})", R"({"code":429})", "{}",
                                   "broken", R"({"code":true})", R"({"code":"2e2"})"})
                QTest::newRow(qPrintable(QString::number(m) + "-" + QString::number(++row)))
                    << m << b << 200
                    << (row <= 2   ? CommentWriteOutcome::Acknowledged
                        : row <= 6 ? CommentWriteOutcome::Rejected
                                   : CommentWriteOutcome::Unknown);
            QTest::newRow(qPrintable(QString::number(m) + "-http429"))
                << m << QByteArray("{}") << 429 << CommentWriteOutcome::Unknown;
            QTest::newRow(qPrintable(QString::number(m) + "-http502"))
                << m << QByteArray("{}") << 502 << CommentWriteOutcome::Unknown;
        }
    }
    void responses()
    {
        QFETCH(int, mode);
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(CommentWriteOutcome, outcome);
        Harness h({Plan{mode < 2 ? body : commentEncrypted(body), status}});
        QSignalSpy done(&h.gateway, &ICommentWriteGateway::completed);
        QVERIFY(h.submit(mode));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).step, CommentWriteStep::Writing);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(h.tokens->requests.size(), mode == 2 || mode == 3 ? 1 : 0);
        QTest::qWait(1);
        QCOMPARE(done.size(), 1);
    }
    void gates()
    {
        for (int m = 0; m < 5; ++m) {
            Harness guest({}, {}, false);
            QSignalSpy g(&guest.gateway, &ICommentWriteGateway::completed);
            QVERIFY(guest.submit(m));
            QTRY_COMPARE(g.size(), 1);
            QCOMPARE(result(g).error, CommentWriteError::AuthenticationRequired);
            QVERIFY(guest.tokens->requests.isEmpty());
            QVERIFY(guest.http->requests.isEmpty());
            Harness noCookie({}, {}, true, false);
            QSignalSpy c(&noCookie.gateway, &ICommentWriteGateway::completed);
            QVERIFY(noCookie.submit(m));
            QTRY_COMPARE(c.size(), 1);
            QCOMPARE(result(c).error, CommentWriteError::AuthenticationRequired);
            QVERIFY(noCookie.tokens->requests.isEmpty());
            QVERIFY(noCookie.http->requests.isEmpty());
            Harness foreign;
            QSignalSpy f(&foreign.gateway, &ICommentWriteGateway::completed);
            QVERIFY(foreign.gateway.submit(foreign.session.snapshot().token, "202", mutation(m)));
            QTRY_COMPARE(f.size(), 1);
            QCOMPARE(result(f).error, CommentWriteError::InvalidRequest);
            QVERIFY(foreign.tokens->requests.isEmpty());
            QCOMPARE(foreign.http->requests.size(), 1);
            if (m >= 2) {
                Harness key({}, {}, true, true, false);
                QSignalSpy k(&key.gateway, &ICommentWriteGateway::completed);
                QVERIFY(key.submit(m));
                QTRY_COMPARE(k.size(), 1);
                QCOMPARE(result(k).outcome, CommentWriteOutcome::NotSent);
                QCOMPARE(result(k).error, CommentWriteError::ProtocolFailure);
                QVERIFY(key.tokens->requests.isEmpty());
                QCOMPARE(key.http->requests.size(), 1);
            }
        }
    }
    void cancellation_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("phase");
        QTest::addColumn<bool>("logout");
        for (int m = 0; m < 5; ++m)
            for (int p = 0; p < 3; ++p)
                if (p != 1 || m == 2 || m == 3)
                    for (bool logout : {false, true})
                        QTest::newRow(qPrintable(QString::number(m) + "-" + QString::number(p) +
                                                 "-" + QString::number(logout)))
                            << m << p << logout;
    }
    void cancellation()
    {
        QFETCH(int, mode);
        QFETCH(int, phase);
        QFETCH(bool, logout);
        Harness h({Plan{"", 200, -1}}, phase == 1 ? QList<Plan>{Plan{"", 200, -1}} : QList<Plan>{});
        QSignalSpy done(&h.gateway, &ICommentWriteGateway::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.submit(mode);
        QVERIFY(id);
        if (phase == 1)
            QTRY_COMPARE(h.tokens->requests.size(), 1);
        else if (phase == 2)
            QTRY_COMPARE(h.http->requests.size(), 2);
        if (logout)
            h.session.signOut();
        else
            h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome,
                 phase == 2 ? CommentWriteOutcome::Unknown : CommentWriteOutcome::NotSent);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        const auto n = h.http->requests.size();
        QTest::qWait(5);
        QCOMPARE(h.http->requests.size(), n);
        QCOMPARE(done.size(), 1);
    }
    void transportTokenBoundary()
    {
        auto manager = std::make_unique<FakeManager>();
        auto* observed = manager.get();
        HttpTransport transport(std::move(manager));
        QSignalSpy done(&transport, &HttpTransport::completed);
        TransportRequest r;
        r.protocol = Protocol::Xeapi;
        r.logicalPath = "/api/resource/comments/add";
        r.formBody = "synthetic";
        QVERIFY(transport.post(r));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(qvariant_cast<TransportResult>(done.last()[1]).error,
                 TransportError::InvalidRequest);
        r.commentV3Token = "x\ny";
        QVERIFY(transport.post(r));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(qvariant_cast<TransportResult>(done.last()[1]).error,
                 TransportError::InvalidRequest);
        r.commentV3Token = "synthetic-v3";
        r.logicalPath = "/api/resource/comments/delete";
        QVERIFY(transport.post(r));
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(qvariant_cast<TransportResult>(done.last()[1]).error,
                 TransportError::InvalidRequest);
        r.logicalPath = "/api/resource/comments/add";
        r.xeapiHeaders = {{"X-antiCheatToken", "synthetic-v3"}};
        QVERIFY(transport.post(r));
        QTRY_COMPARE(done.size(), 4);
        QCOMPARE(qvariant_cast<TransportResult>(done.last()[1]).error,
                 TransportError::InvalidRequest);
        QVERIFY(observed->requests.isEmpty());
        r.xeapiHeaders = {};
        observed->plans = {Plan{"synthetic"}};
        QVERIFY(transport.post(r));
        QTRY_COMPARE(done.size(), 5);
        QCOMPARE(qvariant_cast<TransportResult>(done.last()[1]).error, TransportError::None);
        QCOMPARE(observed->requests.size(), 1);
        QCOMPARE(observed->requests.first().rawHeader("X-antiCheatToken"),
                 QByteArray("synthetic-v3"));
        Harness h({}, {}, true, false);
        QSignalSpy api(&h.client, &SessionApiClient::completed);
        const auto req = commentWriteRequest(mutation(4));
        QVERIFY(h.client.submit(h.session.snapshot().token, req->operation, req->parameters));
        QTRY_COMPARE(api.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(api.last()[2]).error, ApiError::Cookie);
        QVERIFY(h.http->requests.isEmpty());
    }
    void clientLoss_data()
    {
        QTest::addColumn<bool>("tokenPhase");
        QTest::newRow("token") << true;
        QTest::newRow("writing") << false;
    }
    void clientLoss()
    {
        QFETCH(bool, tokenPhase);
        SessionCoordinator session;
        XeapiKeyRing ring;
        QVERIFY(ring.replace(commentTestKey()));
        auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        const auto token = session.snapshot().token;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        FakeManager* http = nullptr;
        CommentTokenManager* issuer = nullptr;
        auto client = std::make_unique<SessionApiClient>(
            Harness::transport(http, {seed, Plan{"", 200, -1}}), "synthetic-comment");
        client->setXeapiKeyRing(&ring);
        QVERIFY(client->advanceTo(token));
        QSignalSpy seeded(client.get(), &SessionApiClient::completed);
        QVERIFY(client->submit(token, ApiOperation::QrKey));
        QTRY_COMPARE(seeded.size(), 1);
        CommentWriteGateway gateway(session, *client, Harness::issuer(issuer, {Plan{"", 200, -1}}));
        QSignalSpy done(&gateway, &ICommentWriteGateway::completed);
        const auto m = mutation(tokenPhase ? 2 : 0);
        QVERIFY(gateway.submit(token, "101", m));
        if (tokenPhase)
            QTRY_COMPARE(issuer->requests.size(), 1);
        else
            QTRY_COMPARE(http->requests.size(), 2);
        client.reset();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome,
                 tokenPhase ? CommentWriteOutcome::NotSent : CommentWriteOutcome::Unknown);
        QCOMPARE(result(done).error, CommentWriteError::Network);
        QCOMPARE(result(done).step,
                 tokenPhase ? CommentWriteStep::Token : CommentWriteStep::Writing);
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QTRY_VERIFY(issuer->findChildren<QNetworkReply*>().isEmpty());
        QTest::qWait(5);
        QCOMPARE(done.size(), 1);
    }
    void sameTokenAndWrongDelivery()
    {
        for (int change = 0; change < 3; ++change) {
            BackendFacade f;
            ready(f);
            QSignalSpy commands(&f, &BackendFacade::commentWriteRequested),
                done(&f, &BackendFacade::commentWriteCompleted);
            QVERIFY(f.mutateComment(mutation(2)));
            const auto c = qvariant_cast<CommentWriteCommand>(commands.first()[0]);
            if (change < 2) {
                BackendEvent e;
                e.kind = BackendEvent::Kind::Session;
                e.session = f.snapshot();
                if (change == 0)
                    e.session.accountId = "202";
                else
                    e.session.phase = SessionPhase::Guest;
                f.deliver(e);
                QTRY_COMPARE(done.size(), 1);
                QCOMPARE(result(done).outcome, CommentWriteOutcome::Unknown);
                QCOMPARE(result(done).error, CommentWriteError::StaleContext);
            } else {
                f.deliverCommentWrite({c.id,
                                       c.generation + 1,
                                       c.context,
                                       {{},
                                        CommentWriteOutcome::Acknowledged,
                                        CommentWriteError::None,
                                        CommentWriteStep::Writing}});
                QCOMPARE(done.size(), 1);
                QCOMPARE(result(done).outcome, CommentWriteOutcome::Unknown);
            }
            f.deliverCommentWrite({c.id,
                                   c.generation,
                                   c.context,
                                   {{},
                                    CommentWriteOutcome::Acknowledged,
                                    CommentWriteError::None,
                                    CommentWriteStep::Writing}});
            QCOMPARE(done.size(), 1);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), c.context);
        }
    }
    void freshTokensAndIssuerBounds()
    {
        Harness h(
            {Plan{commentEncrypted(R"({"code":200})")}, Plan{commentEncrypted(R"({"code":200})")}},
            {Plan{R"(null([200,1,"synthetic-first"]))"},
             Plan{R"(null([200,2,"synthetic-second"]))"}});
        QSignalSpy done(&h.gateway, &ICommentWriteGateway::completed);
        QVERIFY(h.submit(2));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(h.submit(3));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(h.tokens->requests.size(), 2);
        QCOMPARE(h.http->requests[1].rawHeader("X-antiCheatToken"), QByteArray("synthetic-first"));
        QCOMPARE(h.http->requests[2].rawHeader("X-antiCheatToken"), QByteArray("synthetic-second"));
        auto manager = std::make_unique<CommentTokenManager>();
        auto* observed = manager.get();
        for (int i = 0; i < 8; ++i)
            observed->plans.append(Plan{"", 200, -1});
        CommentV3Issuer issuer(std::move(manager));
        int completed = 0;
        QObject::connect(&issuer, &CommentV3Issuer::completed, &issuer,
                         [&](quint64, CommentTokenResult r) {
                             QCOMPARE(r.error, CommentTokenError::Canceled);
                             QVERIFY(r.token.isEmpty());
                             ++completed;
                         });
        QList<quint64> ids;
        for (int i = 0; i < 8; ++i) {
            ids.append(issuer.acquire());
            QVERIFY(ids.last());
        }
        QVERIFY(!issuer.acquire());
        for (auto id : ids)
            issuer.cancel(id);
        QTRY_COMPARE(completed, 8);
        QVERIFY(observed->requests.isEmpty());
    }
    void tokenGuardAndFacade()
    {
        Harness h;
        QSignalSpy d(&h.client, &SessionApiClient::completed);
        const auto request = commentWriteRequest(mutation(2));
        QVERIFY(
            h.client.submit(h.session.snapshot().token, request->operation, request->parameters));
        QTRY_COMPARE(d.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(d.last()[2]).error, ApiError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1);
        QVERIFY(
            h.client.submit(h.session.snapshot().token, ApiOperation::QrKey, {}, "synthetic-v3"));
        QTRY_COMPARE(d.size(), 2);
        QCOMPARE(qvariant_cast<ApiResult>(d.last()[2]).error, ApiError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1);
        QVERIFY(commentTokenFrom(R"( null([200,1,"synthetic-v3"]); )") == "synthetic-v3");
        QVERIFY(!validCommentV3Token("x\ny"));
        QVERIFY(!validCommentV3Token(QByteArray(4097, 'x')));
        BackendFacade f;
        ready(f);
        QSignalSpy commands(&f, &BackendFacade::commentWriteRequested),
            done(&f, &BackendFacade::commentWriteCompleted),
            reads(&f, &BackendFacade::musicRequested);
        for (int i = 0; i < 8; ++i)
            QVERIFY(f.mutateComment(mutation(i % 5)));
        QVERIFY(!f.mutateComment(mutation()));
        const auto c = qvariant_cast<CommentWriteCommand>(commands.first()[0]);
        QVERIFY(!f.mutateComment(c.mutation));
        CommentWriteResult premature;
        premature.outcome = CommentWriteOutcome::Acknowledged;
        premature.step = CommentWriteStep::Token;
        f.deliverCommentWrite({c.id, c.generation, c.context, premature});
        QCOMPARE(result(done).outcome, CommentWriteOutcome::Unknown);
        const auto fresh = f.mutateComment(mutation());
        QVERIFY(fresh);
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 9);
        QCOMPARE(result(done).outcome, CommentWriteOutcome::Unknown);
        QCOMPARE(result(done).step, CommentWriteStep::Unobserved);
        QVERIFY(reads.isEmpty());
        f.deliverCommentWrite({fresh,
                               c.generation,
                               c.context,
                               {{},
                                CommentWriteOutcome::Acknowledged,
                                CommentWriteError::None,
                                CommentWriteStep::Writing}});
        QCOMPARE(done.size(), 9);
    }
};
QTEST_GUILESS_MAIN(CommentWritesTest)
#include "comment_writes_test.moc"
