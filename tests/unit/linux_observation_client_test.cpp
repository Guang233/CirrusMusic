#include "infrastructure/ipc/linux_observation_client.h"
#include "infrastructure/ipc/linux_observation_endpoint.h"
#include "infrastructure/ipc/playback_observation_codec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace cirrus::infrastructure::ipc;
namespace
{
struct Fixture {
    QTemporaryDir root{QDir::tempPath() + "/obc-XXXXXX"};
    QUuid instance = QUuid::createUuid();
    std::unique_ptr<LinuxObservationEndpoint> endpoint;
    Fixture()
    {
        LinuxEndpointError e;
        endpoint =
            LinuxObservationEndpoint::create({true, root.path(), "ob-client-test", instance}, e);
    }
    LinuxObservationClientConfig config() const
    {
        return {root.path(), "ob-client-test", instance};
    }
    bool overwrite(QByteArray b)
    {
        if (!endpoint)
            return false;
        QFile f(endpoint->manifestPath());
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(b) == b.size();
    }
};
QJsonObject hello(QUuid instance)
{
    QJsonArray tracks;
    for (auto t : PlaybackObservationCodec::trackNames())
        tracks.append(t);
    return {{"v", 1},
            {"kind", "response"},
            {"id", "hello"},
            {"ok", true},
            {"instanceId", instance.toString(QUuid::WithoutBraces)},
            {"methods",
             QJsonArray{"hello", "getSnapshot", "subscribe", "unsubscribe", "getLyricsSlice"}},
            {"tracks", tracks},
            {"limits", QJsonObject{{"requestBytes", 4096},
                                   {"frameBytes", 1048576},
                                   {"snapshotBytes", 262144},
                                   {"documentBytes", 8388608},
                                   {"maxLines", 128},
                                   {"clients", 8},
                                   {"pendingRequests", 4},
                                   {"framesPerSecond", 16},
                                   {"burstFrames", 16},
                                   {"helloMs", 5000},
                                   {"partialFrameMs", 2000},
                                   {"inputBytes", 16384},
                                   {"clientBytes", 2097152},
                                   {"allClientBytes", 16777216},
                                   {"slowMs", 5000}}}};
}
struct Fd {
    int fd = -1;
    ~Fd()
    {
        if (fd >= 0)
            ::close(fd);
    }
};
} // namespace
class LinuxObservationClientTest final : public QObject
{
    Q_OBJECT
  private slots:
    void validPinnedDiscoveryAndTwoSidedKernelPeer()
    {
        Fixture f;
        QVERIFY(f.endpoint);
        LinuxObservationClientError e;
        auto c = LinuxObservationClient::connect(f.config(), e);
        QVERIFY(c);
        QCOMPARE(e, LinuxObservationClientError::None);
        Fd peer;
        peer.fd = f.endpoint->acceptSameUser();
        QVERIFY(peer.fd >= 0);
        QVERIFY(linuxSameUserPeer(peer.fd));
        QCOMPARE(c->advance(), LinuxObservationClientState::Hello);
        std::array<char, 128> req{};
        QVERIFY(::recv(peer.fd, req.data(), req.size(), MSG_DONTWAIT) > 0);
        auto reply = PlaybackObservationCodec::frame(
            QJsonDocument(hello(f.instance)).toJson(QJsonDocument::Compact));
        QVERIFY(::send(peer.fd, reply.data(), size_t(reply.size()), MSG_DONTWAIT | MSG_NOSIGNAL) ==
                reply.size());
        QTRY_COMPARE(c->advance(), LinuxObservationClientState::Verified);
        Fd verified;
        verified.fd = c->takeVerifiedDescriptor();
        QVERIFY(verified.fd >= 0);
        QVERIFY(linuxSameUserPeer(verified.fd));
        QVERIFY(::fcntl(verified.fd, F_GETFL) & O_NONBLOCK);
        QVERIFY(::fcntl(verified.fd, F_GETFD) & FD_CLOEXEC);
        QCOMPARE(c->takeVerifiedDescriptor(), -1);
    }
    void malformedManifest_data()
    {
        QTest::addColumn<QByteArray>("replacement");
        QTest::newRow("duplicate") << QByteArray("{\"schema\":1,\"schema\":1}");
        QTest::newRow("invalidUtf8") << QByteArray("\xff", 1);
        QTest::newRow("fraction") << QByteArray("{\"schema\":1.0}");
        QTest::newRow("bom") << QByteArray("\xef\xbb\xbf{}", 5);
        QTest::newRow("oversize") << QByteArray(4097, ' ');
        QTest::newRow("unknownField") << QByteArray("{\"schema\":1,\"extra\":1}");
    }
    void malformedManifest()
    {
        QFETCH(QByteArray, replacement);
        Fixture f;
        QVERIFY(f.endpoint);
        QVERIFY(f.overwrite(replacement));
        LinuxObservationClientError e;
        QVERIFY(!LinuxObservationClient::connect(f.config(), e));
        QVERIFY(e != LinuxObservationClientError::None);
    }
    void wrongManifestIdentityAndUri()
    {
        Fixture f;
        QVERIFY(f.endpoint);
        QFile file(f.endpoint->manifestPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto o = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        const auto original = o;
        LinuxObservationClientError e;
        o["instanceId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(f.overwrite(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QVERIFY(!LinuxObservationClient::connect(f.config(), e));
        QCOMPARE(e, LinuxObservationClientError::InstanceMismatch);
        o = original;
        o["endpoint"] = "tcp://127.0.0.1:1234";
        QVERIFY(f.overwrite(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        QVERIFY(!LinuxObservationClient::connect(f.config(), e));
        QCOMPARE(e, LinuxObservationClientError::Protocol);
    }
    void permissionsAndSymlinkReject_data()
    {
        QTest::addColumn<int>("node");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString("node%1").arg(i))) << i;
    }
    void permissionsAndSymlinkReject()
    {
        QFETCH(int, node);
        Fixture f;
        QVERIFY(f.endpoint);
        const auto manifest = f.endpoint->manifestPath();
        if (node == 0)
            QVERIFY(QFile::setPermissions(f.root.path(), QFile::ReadOwner | QFile::WriteOwner |
                                                             QFile::ExeOwner | QFile::ReadGroup));
        else if (node == 1)
            QVERIFY(QFile::setPermissions(QFileInfo(manifest).path(),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                              QFile::ReadGroup));
        else if (node == 2)
            QVERIFY(QFile::setPermissions(manifest,
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        else if (node == 3)
            QVERIFY(QFile::setPermissions(f.endpoint->socketPath(),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        else {
            auto moved = manifest + ".owned";
            QVERIFY(QFile::rename(manifest, moved));
            QVERIFY(::symlink(QFile::encodeName(moved).constData(),
                              QFile::encodeName(manifest).constData()) == 0);
        }
        LinuxObservationClientError e;
        QVERIFY(!LinuxObservationClient::connect(f.config(), e));
        QCOMPARE(e, LinuxObservationClientError::UnsafePath);
    }
    void badHello_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("otherInstance") << 0;
        QTest::newRow("wrongVersion") << 1;
        QTest::newRow("unknownMethod") << 2;
        QTest::newRow("expandedBudget") << 3;
        QTest::newRow("duplicateJSON") << 4;
    }
    void badHello()
    {
        QFETCH(int, kind);
        Fixture f;
        LinuxObservationClientError e;
        auto c = LinuxObservationClient::connect(f.config(), e);
        QVERIFY(c);
        Fd peer;
        peer.fd = f.endpoint->acceptSameUser();
        QVERIFY(peer.fd >= 0);
        c->advance();
        auto o = hello(f.instance);
        if (kind == 0)
            o["instanceId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (kind == 1)
            o["v"] = 2;
        if (kind == 2)
            o["methods"] = QJsonArray{"control"};
        if (kind == 3) {
            auto limits = o["limits"].toObject();
            limits["requestBytes"] = 65536;
            o["limits"] = limits;
        }
        auto body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        if (kind == 4)
            body = "{\"v\":1,\"v\":1}";
        auto b = PlaybackObservationCodec::frame(body);
        QVERIFY(::send(peer.fd, b.data(), size_t(b.size()), MSG_DONTWAIT | MSG_NOSIGNAL) ==
                b.size());
        QTRY_COMPARE(c->advance(), LinuxObservationClientState::Failed);
        QCOMPARE(c->takeVerifiedDescriptor(), -1);
    }
    void pathChangedAfterConnectFailsClosed()
    {
        Fixture f;
        LinuxObservationClientError e;
        auto c = LinuxObservationClient::connect(f.config(), e);
        QVERIFY(c);
        QVERIFY(QFile::setPermissions(f.endpoint->manifestPath(),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        QCOMPARE(c->advance(), LinuxObservationClientState::Failed);
        QCOMPARE(c->error(), LinuxObservationClientError::UnsafePath);
    }
    void pathChangedAfterHelloCannotTransferOrRevive()
    {
        Fixture f;
        LinuxObservationClientError e;
        auto client = LinuxObservationClient::connect(f.config(), e);
        QVERIFY(client);
        Fd peer;
        peer.fd = f.endpoint->acceptSameUser();
        QVERIFY(peer.fd >= 0);
        client->advance();
        auto reply = PlaybackObservationCodec::frame(
            QJsonDocument(hello(f.instance)).toJson(QJsonDocument::Compact));
        QVERIFY(::send(peer.fd, reply.data(), size_t(reply.size()), MSG_DONTWAIT | MSG_NOSIGNAL) ==
                reply.size());
        QTRY_COMPARE(client->advance(), LinuxObservationClientState::Verified);
        const auto path = f.endpoint->manifestPath();
        QVERIFY(
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        QCOMPARE(client->takeVerifiedDescriptor(), -1);
        QCOMPARE(client->state(), LinuxObservationClientState::Failed);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        QCOMPARE(client->takeVerifiedDescriptor(), -1);
    }
    void noFallbackAndMissingInstance()
    {
        LinuxObservationClientError e;
        QVERIFY(!LinuxObservationClient::connect({}, e));
        QCOMPARE(e, LinuxObservationClientError::InvalidConfig);
        Fixture f;
        auto config = f.config();
        config.instanceId = QUuid::createUuid();
        QVERIFY(!LinuxObservationClient::connect(config, e));
        QCOMPARE(e, LinuxObservationClientError::UnsafePath);
        config = f.config();
        config.applicationNamespace = "../other";
        QVERIFY(!LinuxObservationClient::connect(config, e));
        QCOMPARE(e, LinuxObservationClientError::InvalidConfig);
    }
    void stalledHelloDeadline()
    {
        Fixture f;
        LinuxObservationClientError e;
        auto c = LinuxObservationClient::connect(f.config(), e);
        QVERIFY(c);
        Fd peer;
        peer.fd = f.endpoint->acceptSameUser();
        QVERIFY(peer.fd >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(c->advance(), LinuxObservationClientState::Failed, 5800);
        QCOMPARE(c->error(), LinuxObservationClientError::Timeout);
    }
    void failedClientsDoNotLeakDescriptors()
    {
        Fixture f;
        const auto before =
            QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        QVERIFY(f.overwrite("{}"));
        for (int i = 0; i < 32; ++i) {
            LinuxObservationClientError e;
            QVERIFY(!LinuxObservationClient::connect(f.config(), e));
        }
        const auto after =
            QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        QCOMPARE(after, before);
    }
};
QTEST_GUILESS_MAIN(LinuxObservationClientTest)
#include "linux_observation_client_test.moc"
