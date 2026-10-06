#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/api/comment_operation.h"
#include "infrastructure/netease/music/comment_mapper.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
QList<CommentTarget> targets()
{
    return {SongId{"1"}, PlaylistId{"2"}, AlbumId{"3"}, ProgramId{"4"}, PodcastId{"5"}};
}
QList<MusicQuery> queries()
{
    QList<MusicQuery> q;
    for (const auto& t : targets()) {
        if (commentResourceKind(t) != CommentResourceKind::Podcast)
            q.append(CommentsQuery{t, {2, 0}});
        q.append(HotCommentsQuery{t, {2, 0}});
        for (const auto order :
             {CommentOrder::Recommended, CommentOrder::Hot, CommentOrder::Chronological})
            q.append(SortedCommentsQuery{t, 2, 1, order});
        q.append(CommentFloorQuery{t, {"10"}, 2});
        q.append(CommentInfoQuery{{t}});
    }
    return q;
}
QJsonObject row(int id = 10)
{
    return {{"commentId", id}, {"content", "<b>synthetic plain text</b>"},
            {"time", 100},     {"likedCount", 0},
            {"liked", false},  {"user", QJsonObject{{"userId", 101}, {"nickname", "synthetic"}}}};
}
QJsonObject body(const MusicQuery& q)
{
    const QJsonArray rows{row()};
    if (std::holds_alternative<CommentsQuery>(q))
        return {{"code", 200},
                {"comments", rows},
                {"hotComments", QJsonArray{}},
                {"total", 3},
                {"more", true}};
    if (std::holds_alternative<HotCommentsQuery>(q))
        return {{"code", 200}, {"hotComments", rows}, {"total", 3}, {"more", true}};
    if (std::holds_alternative<SortedCommentsQuery>(q))
        return {{"code", 200},
                {"data",
                 QJsonObject{
                     {"comments", rows}, {"totalCount", 3}, {"hasMore", true}, {"cursor", "100"}}}};
    if (std::holds_alternative<CommentFloorQuery>(q))
        return {{"code", 200},
                {"data", QJsonObject{{"comments", QJsonArray{row(11)}},
                                     {"ownerComment", row()},
                                     {"parentCommentId", 10},
                                     {"totalCount", 3},
                                     {"hasMore", true},
                                     {"time", 200}}}};
    const auto& target = std::get<CommentInfoQuery>(q).targets.first();
    return {{"code", 200},
            {"data", QJsonArray{QJsonObject{{"resourceId", commentTargetId(target)},
                                            {"resourceType", commentResourceType(target)},
                                            {"threadId", commentThreadId(target)},
                                            {"commentCount", 0},
                                            {"commentCountDesc", "synthetic estimate"},
                                            {"liked", false}}}}};
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto m = std::make_unique<FakeManager>();
        out = m.get();
        m->plans = plans;
        return std::make_unique<HttpTransport>(std::move(m));
    }
    Harness(QList<Plan> plans, bool auth = false)
        : client(transport(plans, http), "synthetic-comments")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
};
} // namespace
class CommentsTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < queries().size(); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void contracts()
    {
        QFETCH(int, index);
        const auto q = queries()[index];
        QVERIFY(validMusicQuery(q));
        QVERIFY(isCommentQuery(q));
        QVERIFY(!requiresAuthenticatedMusicQuery(q));
        QVERIFY(!musicQueryAccount(q));
        const auto req = commentRequest(q);
        QVERIFY(req);
        const auto c = operationContract(req->operation, req->parameters);
        QVERIFY(c);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->protocol,
                 req->operation == ApiOperation::SortedComments ? Protocol::Eapi : Protocol::Weapi);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseLimit,
                 (req->operation == ApiOperation::CommentInfo ? 1 : 4) * 1024 * 1024);
        QVERIFY(!requiresAccountCookie(req->operation));
        const auto p = operationPayload(req->operation, req->parameters);
        QVERIFY(p);
        QVERIFY(!p->contains("kind"));
        QVERIFY(!p->contains("uid"));
        // Independent pinned-wire expectations, not values generated by the adapter itself.
        const QStringList threads{"R_SO_4_1", "A_PL_0_2", "R_AL_3_3", "A_DJ_1_4", "A_DR_14_5"};
        const QList<int> wireTypes{4, 0, 3, 1, 14};
        const int resourceIndex = index / 7; // first four have seven modes, podcast has six
        const auto expectedThread = threads[resourceIndex];
        const auto expectedId = QString::number(resourceIndex + 1);
        QCOMPARE(commentThreadId(targets()[resourceIndex]), expectedThread);
        QCOMPARE(commentResourceType(targets()[resourceIndex]), wireTypes[resourceIndex]);
        if (req->operation == ApiOperation::SortedComments) {
            QCOMPARE(c->logicalPath, "/api/v2/resource/comments");
            QCOMPARE(p->value("threadId"), QJsonValue(expectedThread));
        } else if (req->operation == ApiOperation::CommentFloor) {
            QCOMPARE(c->logicalPath, "/api/resource/comment/floor/get");
            QCOMPARE(p->value("threadId"), QJsonValue(expectedThread));
            QCOMPARE(p->value("parentCommentId"), QJsonValue("10"));
            QCOMPARE(p->value("time"), QJsonValue(-1));
        } else if (req->operation == ApiOperation::CommentInfo) {
            QCOMPARE(c->logicalPath, "/api/resource/commentInfo/list");
            QCOMPARE(p->value("resourceType"),
                     QJsonValue(QString::number(wireTypes[resourceIndex])));
            QCOMPARE(p->value("resourceIds"), QJsonValue("[\"" + expectedId + "\"]"));
        } else {
            QCOMPARE(c->logicalPath,
                     (req->operation == ApiOperation::HotComments ? "/api/v1/resource/hotcomments/"
                                                                  : "/api/v1/resource/comments/") +
                         expectedThread);
            QCOMPARE(*p, (QJsonObject{
                             {"rid", expectedId}, {"limit", 2}, {"offset", 0}, {"beforeTime", 0}}));
        }
        if (const auto* s = std::get_if<SortedCommentsQuery>(&q)) {
            QCOMPARE(p->value("pageNo"), QJsonValue(1));
            QCOMPARE(p->value("showInner"), QJsonValue(true));
            QCOMPARE(p->value("sortType"), QJsonValue(s->order == CommentOrder::Recommended ? 99
                                                      : s->order == CommentOrder::Hot       ? 2
                                                                                            : 3));
            QCOMPARE(p->value("cursor"), s->order == CommentOrder::Recommended ? QJsonValue(0)
                                         : s->order == CommentOrder::Hot ? QJsonValue("normalHot#0")
                                                                         : QJsonValue("0"));
        }
        for (const QString name :
             {"host", "uid", "threadId", "type", "crypto", "csrf_token", "e_r", "voiceId"}) {
            auto bad = req->parameters;
            bad.insert(name, "injected");
            QVERIFY(!operationPayload(req->operation, bad));
        }
        auto bad = req->parameters;
        bad.insert("id", "1/../../fake");
        QVERIFY(!operationPayload(req->operation, bad));
        const auto r = mapMusicResponse(q, body(q));
        QVERIFY(!std::holds_alternative<MusicFailure>(r));
    }
    void boundaries()
    {
        static_assert(!std::is_constructible_v<CommentTarget, VoiceId>);
        static_assert(!std::is_constructible_v<CommentTarget, ArtistId>);
        static_assert(!std::is_same_v<CommentId, SongId>);
        QVERIFY((!CommentsQuery{PodcastId{"5"}}.isValid()));
        QVERIFY((!HotCommentsQuery{SongId{"01"}}.isValid()));
        QVERIFY((!CommentFloorQuery{SongId{"1"}, {"0"}}.isValid()));
        QVERIFY((!CommentInfoQuery{{SongId{"1"}, AlbumId{"1"}}}.isValid()));
        QVERIFY((!CommentInfoQuery{{SongId{"1"}, SongId{"1"}}}.isValid()));
        QVERIFY((!SortedCommentsQuery{SongId{"1"}, 20, 2, CommentOrder::Chronological}.isValid()));
        QVERIFY(
            (!SortedCommentsQuery{SongId{"1"}, 20, 1, static_cast<CommentOrder>(99)}.isValid()));
        for (const auto bad : {0, 101})
            QVERIFY((!HotCommentsQuery{SongId{"1"}, {bad, 0}}.isValid()));
        for (const qint64 bad : {qint64(-1), MaximumCommentCursor + 1})
            QVERIFY((!CommentsQuery{SongId{"1"}, {20, 0, bad}}.isValid()));
        for (const auto& q : queries()) {
            const auto req = *commentRequest(q);
            for (const QString name : {"limit", "offset", "before", "page", "time"})
                if (req.parameters.contains(name))
                    for (const QJsonValue v : {QJsonValue(true), QJsonValue(1.5), QJsonValue("2"),
                                               QJsonValue(QJsonValue::Null)}) {
                        auto p = req.parameters;
                        p.insert(name, v);
                        QVERIFY(!operationPayload(req.operation, p));
                    }
        }
        const auto time = commentRequest(SortedCommentsQuery{
            ProgramId{"9007199254740993"}, 100, 2, CommentOrder::Chronological, 42, false});
        QVERIFY(time);
        const auto p = operationPayload(time->operation, time->parameters);
        QVERIFY(p);
        QCOMPARE(p->value("showInner"), QJsonValue(false));
        QCOMPARE(p->value("cursor"), QJsonValue("42"));
        const auto floor = commentRequest(CommentFloorQuery{SongId{"1"}, {"10"}, 2, 0});
        QVERIFY(floor);
        QCOMPARE(operationPayload(floor->operation, floor->parameters)->value("time"),
                 QJsonValue(0));
        const auto wide = commentRequest(CommentInfoQuery{{ProgramId{"9007199254740993"}}});
        QVERIFY(wide);
        QCOMPARE(operationPayload(wide->operation, wide->parameters)->value("resourceIds"),
                 QJsonValue("[\"9007199254740993\"]"));
        auto bad = time->parameters;
        bad.insert("kind", "voice");
        QVERIFY(!operationPayload(time->operation, bad));
        auto unknown = time->parameters;
        unknown.insert("kind", "artist");
        QVERIFY(!operationPayload(time->operation, unknown));
    }
    void valuesAndEmpty()
    {
        CommentsQuery q{SongId{"1"}, {2, 0}};
        auto r = row();
        r.insert("commentId", "9007199254740993");
        r.insert("beReplied", QJsonArray{QJsonObject{{"content", QJsonValue::Null},
                                                     {"beRepliedCommentId", "12"}}});
        r.insert("showFloorComment",
                 QJsonObject{{"replyCount", 3}, {"comments", QJsonArray{row(11)}}});
        const auto result = mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{r, r}}});
        QVERIFY(std::holds_alternative<CommentsPage>(result));
        const auto p = std::get<CommentsPage>(result);
        QCOMPARE(p.comments.size(), 2);
        QCOMPARE(p.comments.first().id.value, "9007199254740993");
        QCOMPARE(p.comments.first().content, std::optional<QString>("<b>synthetic plain text</b>"));
        QCOMPARE(p.comments.first().liked, std::optional<bool>(false));
        QCOMPARE(p.comments.first().likedCount, std::optional<qint64>(0));
        QVERIFY(p.comments.first().quotes);
        QVERIFY(!p.comments.first().quotes->first().content);
        QCOMPARE(p.comments.first().innerReplies->size(), 1);
        QCOMPARE(p.nextBefore, std::optional<qint64>(100));
        QVERIFY(!p.hasMore);
        QVERIFY(!p.total);
        QVERIFY(!p.hotComments);
        QVERIFY(!p.topComments);
        QVERIFY(!p.comments.first().author->avatar.isValid());
        const auto minimal = mapMusicResponse(
            q, {{"code", 200}, {"comments", QJsonArray{QJsonObject{{"commentId", 10}}}}});
        QVERIFY(std::holds_alternative<CommentsPage>(minimal));
        QVERIFY(!std::get<CommentsPage>(minimal).nextBefore);
        QVERIFY(!std::get<CommentsPage>(minimal).comments.first().content);
        for (const auto& query : queries()) {
            auto b = body(query);
            if (std::holds_alternative<CommentsQuery>(query))
                b.insert("comments", QJsonArray{});
            else if (std::holds_alternative<HotCommentsQuery>(query))
                b.insert("hotComments", QJsonArray{});
            else if (std::holds_alternative<CommentInfoQuery>(query))
                b.insert("data", QJsonArray{});
            else {
                auto d = b.value("data").toObject();
                d.insert("comments", QJsonArray{});
                b.insert("data", d);
            }
            QVERIFY(!std::holds_alternative<MusicFailure>(mapMusicResponse(query, b)));
        }
        const CommentInfoQuery batch{{SongId{"1"}, SongId{"2"}, SongId{"3"}}};
        auto b = body(batch);
        auto a = b.value("data").toArray();
        auto o = a.first().toObject();
        o.insert("resourceId", "3");
        o.insert("threadId", "R_SO_4_3");
        a.prepend(o);
        b.insert("data", a);
        const auto info = mapMusicResponse(batch, b);
        QVERIFY(std::holds_alternative<CommentStatistics>(info));
        const auto stats = std::get<CommentStatistics>(info);
        QCOMPARE(stats.items.size(), 2);
        QCOMPARE(commentTargetId(stats.items.first().target), "3");
        QCOMPARE(stats.missing.size(), 1);
        QCOMPARE(commentTargetId(stats.missing.first()), "2");
    }
    void malformed_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QByteArray>("body");
        const QList<QByteArray> cases{
            R"({"code":200})",
            R"({"code":200,"comments":null})",
            R"({"code":200,"comments":[null]})",
            R"({"code":200,"comments":[{"commentId":0}]})",
            R"({"code":200,"comments":[{"commentId":"01"}]})",
            R"({"code":200,"comments":[{"commentId":9007199254740992}]})",
            R"({"code":200,"comments":[{"commentId":10,"content":1}]})",
            R"({"code":200,"comments":[{"commentId":10,"user":[]}]})",
            R"({"code":200,"comments":[{"commentId":10,"user":{"userId":-1}}]})",
            R"({"code":200,"comments":[{"commentId":10,"time":true}]})",
            R"({"code":200,"comments":[{"commentId":10,"liked":1}]})",
            R"({"code":200,"comments":[{"commentId":10,"likedCount":1.5}]})",
            R"({"code":200,"comments":[{"commentId":10,"beReplied":{}}]})",
            R"({"code":200,"comments":[{"commentId":10,"beReplied":[1]}]})",
            R"({"code":200,"comments":[{"commentId":10,"showFloorComment":{"replyCount":0,"comments":[{"commentId":11}]}}]})",
            R"({"code":200,"comments":[],"more":1})",
            R"({"code":200,"comments":[{"commentId":10}],"total":0})",
            R"({"code":200,"comments":[],"threadId":"R_AL_3_1"})",
            R"({"code":200,"comments":[],"resourceType":3})",
            R"({"code":200,"comments":[],"hotComments":{}})",
            R"({"code":200,"comments":[],"topComments":[{}]})"};
        for (int i = 0; i < cases.size(); ++i)
            QTest::newRow(qPrintable(QString("legacy-%1").arg(i))) << 0 << cases[i];
        // index1 hot, index2 recommended sorted, index5 floor, index6 stats in queries().
        QTest::newRow("hot-wrong-array") << 1 << QByteArray(R"({"code":200,"comments":[]})");
        QTest::newRow("sorted-root") << 2 << QByteArray(R"({"code":200,"comments":[]})");
        QTest::newRow("sorted-null") << 2 << QByteArray(R"({"code":200,"data":{"comments":null}})");
        QTest::newRow("sorted-sort")
            << 2 << QByteArray(R"({"code":200,"data":{"comments":[],"sortType":1}})");
        QTest::newRow("sorted-cursor")
            << 2 << QByteArray(R"({"code":200,"data":{"comments":[],"cursor":true}})");
        QTest::newRow("floor-parent")
            << 5
            << QByteArray(R"({"code":200,"data":{"comments":[],"ownerComment":{"commentId":11}}})");
        QTest::newRow("floor-thread")
            << 5 << QByteArray(R"({"code":200,"data":{"comments":[],"threadId":"A_DJ_1_1"}})");
        QTest::newRow("stats-wrapper") << 6 << QByteArray(R"({"code":200,"data":{}})");
        QTest::newRow("stats-unsolicited")
            << 6 << QByteArray(R"({"code":200,"data":[{"resourceId":2}]})");
        QTest::newRow("stats-kind")
            << 6 << QByteArray(R"({"code":200,"data":[{"resourceId":1,"resourceType":0}]})");
        QTest::newRow("stats-count")
            << 6 << QByteArray(R"({"code":200,"data":[{"resourceId":1,"commentCount":1.5}]})");
    }
    void malformed()
    {
        QFETCH(int, index);
        QFETCH(QByteArray, body);
        const auto doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());
        const auto r = mapMusicResponse(queries()[index], doc.object());
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void extraBounds()
    {
        const CommentsQuery q{SongId{"1"}, {1, 0}};
        auto r = row();
        r.insert("content", QString(4097, 'a'));
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{r}}})));
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{row(), row()}}})));
        r = row();
        r.insert("content", QString(QChar::Null));
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{r}}})));
        r = row();
        r.insert("user", QJsonObject{{"nickname", QString(QChar(0xd800))}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{r}}})));
        QJsonArray many;
        for (int i = 0; i < 11; ++i)
            many.append(QJsonObject{});
        r = row();
        r.insert("beReplied", many);
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"comments", QJsonArray{r}}})));
        const CommentInfoQuery stats{{SongId{"1"}, SongId{"2"}}};
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(stats, {{"code", 200},
                                     {"data", QJsonArray{QJsonObject{{"resourceId", 1}},
                                                         QJsonObject{{"resourceId", 1}}}}})));
    }
    void nativeChain_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("auth");
        for (int i = 0; i < queries().size(); ++i)
            for (bool auth : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(auth))) << i << auth;
    }
    void nativeChain()
    {
        QFETCH(int, index);
        QFETCH(bool, auth);
        const auto q = queries()[index];
        const auto req = *commentRequest(q);
        Plan p{QJsonDocument(body(q)).toJson(QJsonDocument::Compact)};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, p}, auth);
        const auto t = h.session.snapshot().token;
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        for (int n = 1; n <= 2; ++n) {
            QVERIFY(h.service.request(t, q));
            QTRY_COMPARE(done.size(), n);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), t);
            const auto c = *operationContract(req.operation, req.parameters);
            const auto http = h.http->requests.last();
            QCOMPARE(http.url().host(),
                     c.protocol == Protocol::Eapi ? "interfacepc.music.163.com" : "music.163.com");
            QCOMPARE(http.url().path(),
                     (c.protocol == Protocol::Eapi ? "/eapi" : "/weapi") + c.logicalPath.mid(4));
            QVERIFY(!http.rawHeader("Cookie").contains("synthetic-rotation"));
            if (auth)
                QVERIFY(http.rawHeader("Cookie").contains("synthetic-original"));
            if (c.protocol == Protocol::Eapi) {
                const auto decoded = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(decoded);
                QCOMPARE(decoded->logicalPath, c.logicalPath);
                auto payload = decoded->payload;
                payload.remove("header");
                QCOMPARE(payload.take("e_r"), QJsonValue(false)); // fixed Pc profile envelope
                QCOMPARE(payload, *operationPayload(req.operation, req.parameters));
            } else
                QVERIFY(h.http->bodies.last().contains("&encSecKey="));
        }
        QCOMPARE(h.http->requests.size(), auth ? 3 : 2);
    }
    void cancelStaleRate_data()
    {
        QTest::addColumn<int>("index");
        for (int i : {0, 7, 14, 21, 1, 2, 5, 6})
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cancelStaleRate()
    {
        QFETCH(int, index);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        const auto id = h.service.request(t, queries()[index]);
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 1);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(t, queries()[index]));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), t);
        Harness rate({Plan{"", 429}});
        QSignalSpy r(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, queries()[index]));
        QTRY_COMPARE(r.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(r)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(r)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 1);
    }
    void rejectNoFallback()
    {
        Harness h({Plan{R"({"code":-462})"}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, queries()[2]));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::ProviderRejected);
        QCOMPARE(std::get<MusicFailure>(result(done)).providerCode, std::optional<int>(-462));
        QTest::qWait(10);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void facade()
    {
        BackendFacade f;
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        const SessionToken t{QUuid::createUuid(), 1};
        e.session = {t, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy req(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(f.comments({SongId{"1"}}));
        QVERIFY(f.hotComments({PodcastId{"5"}}));
        QVERIFY(f.sortedComments({AlbumId{"3"}}));
        QVERIFY(f.commentFloor({ProgramId{"4"}, {"10"}}));
        QVERIFY(f.commentInfo({{PlaylistId{"2"}}}));
        QVERIFY(!f.comments({PodcastId{"5"}}));
        QVERIFY(!f.comments({SongId{"0"}}));
        while (req.size() < 16)
            QVERIFY(f.comments({SongId{"1"}}));
        QVERIFY(!f.hotComments({SongId{"1"}}));
        const auto original = qvariant_cast<MusicCommand>(req.first()[0]);
        QCOMPARE(original.context, t);
        QVERIFY(f.signOut());
        QVERIFY(!f.snapshot().token.isValid()); // immediate intent barrier
        QTRY_COMPARE(done.size(), 16);          // invalidation completion is queued
        for (const auto& completed : done) {
            const auto failure = qvariant_cast<MusicResult>(completed[2]);
            QCOMPARE(std::get<MusicFailure>(failure).error, MusicError::StaleContext);
            QCOMPARE(qvariant_cast<SessionToken>(completed[1]), t);
        }
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
        f.deliverMusic({original.id, original.generation, original.context, CommentsPage{}});
        QCOMPARE(done.size(), 16);
    }
};
QTEST_GUILESS_MAIN(CommentsTest)
#include "comments_test.moc"
