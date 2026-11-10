#include "../support/fake_http.h"
#include "../support/synthetic_user_profile.h"
#include "application/music/music_query_service.h"
#include "domain/music/collection_types.h"
#include "infrastructure/netease/api/user_profile_operation.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/user_profile_mapper.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
MusicResult last(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
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
    explicit Harness(QList<Plan> plans) : client(transport(plans, http), "synthetic-user-profile")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
    }
};
QJsonObject parse(const char* json) { return QJsonDocument::fromJson(json).object(); }
} // namespace

class UserProfileTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts()
    {
        const QString uid = "37179178";
        const auto detail = userProfileRequest({UserDetailQuery{uid}});
        QVERIFY(detail);
        QCOMPARE(detail->operation, ApiOperation::UserDetail);
        auto c = operationContract(detail->operation, detail->parameters);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, QString("/api/v1/user/detail/37179178"));
        QCOMPARE(c->protocol, Protocol::Weapi);
        QCOMPARE(*operationPayload(detail->operation, detail->parameters), QJsonObject{});

        const auto follows = userProfileRequest({UserFollowsQuery{uid, {20, 40}}});
        c = operationContract(follows->operation, follows->parameters);
        QCOMPARE(c->logicalPath, QString("/api/user/getfollows/37179178"));
        QCOMPARE(*operationPayload(follows->operation, follows->parameters),
                 (QJsonObject{{"offset", 40}, {"limit", 20}, {"order", true}}));

        // Only the eapi path without the uid honors limit and cursor (measured).
        const auto fans = userProfileRequest({UserFansQuery{uid, 20, 1704693480018}});
        c = operationContract(fans->operation, fans->parameters);
        QCOMPARE(c->logicalPath, QString("/api/user/getfolloweds"));
        QCOMPARE(c->protocol, Protocol::Eapi);
        QCOMPARE(*operationPayload(fans->operation, fans->parameters),
                 (QJsonObject{{"userId", uid},
                              {"time", "1704693480018"},
                              {"limit", 20},
                              {"offset", 0},
                              {"getcounts", "true"}}));

        const auto playlists = userProfileRequest({UserPlaylistsQuery{uid}});
        c = operationContract(playlists->operation, playlists->parameters);
        QCOMPARE(c->logicalPath, QString("/api/user/playlist"));
        QCOMPARE(
            *operationPayload(playlists->operation, playlists->parameters),
            (QJsonObject{{"uid", uid}, {"limit", 100}, {"offset", 0}, {"includeVideo", false}}));

        const auto ranking = userProfileRequest({UserRankingQuery{uid, RankingPeriod::LastWeek}});
        c = operationContract(ranking->operation, ranking->parameters);
        QCOMPARE(c->logicalPath, QString("/api/v1/play/record"));
        QCOMPARE(*operationPayload(ranking->operation, ranking->parameters),
                 (QJsonObject{{"uid", uid}, {"type", 1}}));
        for (auto op :
             {ApiOperation::UserDetail, ApiOperation::UserPlaylists, ApiOperation::UserFollows,
              ApiOperation::UserFans, ApiOperation::UserRanking}) {
            QVERIFY(!requiresAccountCookie(op));
            QVERIFY(retriesLostConnection(op));
        }
    }
    // Follow writes: the signed-in account's, never resent, the uid in the path.
    void followWrites()
    {
        const QJsonObject p{{"uid", "37179178"}};
        auto c = operationContract(ApiOperation::UserFollow, p);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, QString("/api/user/follow/37179178"));
        QCOMPARE(c->protocol, Protocol::Weapi);
        c = operationContract(ApiOperation::UserUnfollow, p);
        QCOMPARE(c->logicalPath, QString("/api/user/delfollow/37179178"));
        QCOMPARE(*operationPayload(ApiOperation::UserFollow, p), QJsonObject{});
        QVERIFY(!operationPayload(ApiOperation::UserFollow, {{"uid", "1"}, {"x", 1}}));
        QVERIFY(!operationContract(ApiOperation::UserUnfollow, {{"uid", "0"}}));
        for (auto op : {ApiOperation::UserFollow, ApiOperation::UserUnfollow}) {
            QVERIFY(requiresAccountCookie(op));
            QVERIFY(!retriesLostConnection(op));
        }
        QVERIFY((UserFollow{"1", CollectionAction::Remove}.isValid()));
        QVERIFY(!UserFollow{"x"}.isValid());
    }
    void invalidRequests()
    {
        QVERIFY(!userProfileRequest({UserDetailQuery{"0"}}));
        QVERIFY(!userProfileRequest({UserDetailQuery{"abc"}}));
        QVERIFY(!userProfileRequest({UserFansQuery{"1", 0, 0}}));
        QVERIFY(!userProfileRequest({UserFansQuery{"1", 20, -1}}));
        QVERIFY(!userProfileRequest({UserFollowsQuery{"1", {0, 0}}}));
        // The payloads are closed: extra, missing or non-canonical parameters fail.
        QVERIFY(!operationPayload(ApiOperation::UserDetail, {{"uid", "1"}, {"x", 1}}));
        QVERIFY(!operationPayload(ApiOperation::UserFans,
                                  {{"uid", "1"}, {"limit", 20}, {"time", "01"}}));
        QVERIFY(
            !operationPayload(ApiOperation::UserFans, {{"uid", "1"}, {"limit", 20}, {"time", 0}}));
        QVERIFY(!operationPayload(ApiOperation::UserRanking, {{"uid", "1"}, {"type", 2}}));
        QVERIFY(!operationContract(ApiOperation::UserFollows, {{"uid", "x"}}));
    }
    void profile()
    {
        const UserProfileQuery q{UserDetailQuery{"37179178"}};
        const auto r = mapUserProfileResponse(q, parse(detailJson));
        const auto* page = std::get_if<UserProfilePage>(&r);
        QVERIFY(page);
        const auto& p = std::get<UserProfile>(page->content);
        QCOMPARE(p.nickname, QString("N"));
        QCOMPARE(p.level, std::optional<qint64>(10));
        QCOMPARE(p.fans, std::optional<qint64>(7540967));
        QCOMPARE(p.playlistCount, std::optional<qint64>(2));
        QCOMPARE(p.artist->value, QString("1007170"));
        QCOMPARE(p.rankingOpen, std::optional<bool>(false));
        QVERIFY(!p.avatar.isEmpty());
        QVERIFY(matchingUserProfilePage(q, *page));

        auto plain = parse(detailJson);
        auto profile = plain.value("profile").toObject();
        profile.insert("artistId", 0);
        plain.insert("profile", profile);
        QVERIFY(!std::get<UserProfile>(
                     std::get<UserProfilePage>(mapUserProfileResponse(q, plain)).content)
                     .artist);
        // Another user's profile is not this one.
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapUserProfileResponse({UserDetailQuery{"5"}}, parse(detailJson))));
        QVERIFY(std::holds_alternative<MusicFailure>(mapUserProfileResponse(q, {{"code", 200}})));
    }
    void lists()
    {
        const UserProfileQuery fansQuery{UserFansQuery{"2044521815", 20, 0}};
        auto r = mapUserProfileResponse(fansQuery, parse(fansJson));
        const auto& fans = std::get<UserList>(std::get<UserProfilePage>(r).content);
        QCOMPARE(fans.users.size(), 2);
        QCOMPARE(fans.total, std::optional<qint64>(99));
        QCOMPARE(fans.hasMore, std::optional<bool>(true));
        QCOMPARE(fans.nextCursor, std::optional<qint64>(1780910723689));
        QVERIFY(fans.users[0].signature.isEmpty());
        QVERIFY(fans.users[1].avatar.isEmpty());
        QCOMPARE(fans.users[1].followed, std::optional<bool>(true));
        // More rows than asked for, or a cursor going forward, is not the page asked for.
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapUserProfileResponse({UserFansQuery{"2044521815", 1, 0}}, parse(fansJson))));
        auto swapped = parse(fansJson);
        auto rows = swapped.value("followeds").toArray();
        swapped.insert("followeds", QJsonArray{rows[1], rows[0]});
        QVERIFY(std::holds_alternative<MusicFailure>(mapUserProfileResponse(fansQuery, swapped)));

        r = mapUserProfileResponse({UserFollowsQuery{"2044521815"}}, parse(followsJson));
        const auto& follows = std::get<UserList>(std::get<UserProfilePage>(r).content);
        QCOMPARE(follows.users.size(), 1);
        QCOMPARE(follows.users[0].userId, QString("7864598263"));
        QVERIFY(!follows.nextCursor);
        QVERIFY(!follows.total);
    }
    void sharedMappers()
    {
        const UserProfileQuery q{UserPlaylistsQuery{"37179178"}};
        auto r = mapUserProfileResponse(q, parse(playlistsJson));
        const auto& lists = std::get<UserPlaylists>(std::get<UserProfilePage>(r).content);
        QCOMPARE(lists.playlists.size(), 2);
        QCOMPARE(lists.playlists[0].accountLikedSongs, std::optional<bool>(true));
        QCOMPARE(*lists.playlists[1].creatorId, QString("5"));
        QCOMPARE(lists.hasMore, std::optional<bool>(false));

        const UserProfileQuery rq{UserRankingQuery{"37179178", RankingPeriod::LastWeek}};
        r = mapUserProfileResponse(rq, parse(rankingJson));
        const auto& ranking = std::get<UserRanking>(std::get<UserProfilePage>(r).content);
        QCOMPARE(ranking.songs.size(), 1);
        QCOMPARE(ranking.songs[0].score, std::optional<double>(100));
        QVERIFY(std::holds_alternative<MusicFailure>(mapUserProfileResponse(
            {UserRankingQuery{"37179178", RankingPeriod::AllTime}}, parse(rankingJson))));
    }
    void matching()
    {
        const UserProfileQuery detail{UserDetailQuery{"1"}};
        const UserProfileQuery fans{UserFansQuery{"1"}};
        QVERIFY(!matchingUserProfilePage(detail, {detail, UserProfileHidden{}}));
        QVERIFY(matchingUserProfilePage(fans, {fans, UserProfileHidden{}}));
        QVERIFY(matchingUserProfilePage(fans, {fans, UserList{}}));
        QVERIFY(!matchingUserProfilePage(fans, {fans, UserRanking{}}));
        QVERIFY(!matchingUserProfilePage(fans, {UserProfileQuery{UserFansQuery{"2"}}, UserList{}}));
        QVERIFY(
            !matchingUserProfilePage(fans, {UserProfileQuery{UserFollowsQuery{"1"}}, UserList{}}));
    }
    // Guests read through the real client; privacy answers become UserProfileHidden.
    void guestReadsAndPrivacy()
    {
        Harness h({Plan{fansJson}, Plan{hiddenListJson}, Plan{hiddenRankingJson}, Plan{missingJson},
                   Plan{hiddenListJson}});
        const auto token = h.session.snapshot().token;
        QSignalSpy spy(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(token, UserProfileQuery{UserFansQuery{"2044521815", 20, 0}}));
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(std::holds_alternative<UserList>(std::get<UserProfilePage>(last(spy)).content));
        QCOMPARE(h.http->requests[0].url().path(), QString("/eapi/user/getfolloweds"));

        QVERIFY(h.service.request(token, UserProfileQuery{UserFollowsQuery{"1"}}));
        QTRY_COMPARE(spy.size(), 2);
        QVERIFY(std::holds_alternative<UserProfileHidden>(
            std::get<UserProfilePage>(last(spy)).content));
        QVERIFY(h.service.request(token, UserProfileQuery{UserRankingQuery{"1"}}));
        QTRY_COMPARE(spy.size(), 3);
        QVERIFY(std::holds_alternative<UserProfileHidden>(
            std::get<UserProfilePage>(last(spy)).content));
        // A missing user is a failure with the provider's code, never "hidden".
        QVERIFY(h.service.request(token, UserProfileQuery{UserDetailQuery{"99999999999999"}}));
        QTRY_COMPARE(spy.size(), 4);
        const auto missing = std::get<MusicFailure>(last(spy));
        QCOMPARE(missing.error, MusicError::ProviderRejected);
        QCOMPARE(missing.providerCode, std::optional<int>(404));
        QVERIFY(h.service.request(token, UserProfileQuery{UserDetailQuery{"1"}}));
        QTRY_COMPARE(spy.size(), 5);
        QVERIFY(std::holds_alternative<MusicFailure>(last(spy)));
        QCOMPARE(h.http->requests.size(), 5);
    }
};

QTEST_GUILESS_MAIN(UserProfileTest)
#include "user_profile_test.moc"
