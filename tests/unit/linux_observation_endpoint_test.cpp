#include "infrastructure/ipc/linux_observation_endpoint.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace cirrus::infrastructure::ipc;
namespace
{
struct TestFd {
    int fd = -1;
    ~TestFd()
    {
        if (fd >= 0)
            ::close(fd);
    }
};
LinuxObservationEndpointConfig config(const QTemporaryDir& root)
{
    return {true, root.path(), "ob-test", QUuid::createUuid()};
}
QString spacePath(const LinuxObservationEndpointConfig& c)
{
    return c.runtimeRoot + '/' + c.applicationNamespace;
}
QString leafPath(const LinuxObservationEndpointConfig& c)
{
    return spacePath(c) + "/i-" + c.instanceId.toString(QUuid::WithoutBraces);
}
bool writeFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("synthetic foreign file") == 22;
}
int dial(const QString& path)
{
    TestFd fd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto bytes = QFile::encodeName(path);
    if (bytes.size() >= qsizetype(sizeof(address.sun_path)))
        return -1;
    std::memcpy(address.sun_path, bytes.constData(), size_t(bytes.size()) + 1);
    if (fd.fd < 0 || ::connect(fd.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        return -1;
    const auto result = fd.fd;
    fd.fd = -1;
    return result;
}
} // namespace
class LinuxObservationEndpointTest final : public QObject
{
    Q_OBJECT
  private slots:
    void disabledHasNoSideEffects()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto c = config(root);
        c.enabled = false;
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::Disabled);
        QVERIFY(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void invalidNamespace_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto& s :
             QStringList{"", ".", "..", "../outside", "/absolute", "a/b", "a b", QString(65, 'a'),
                         QString::fromUtf8("雲"), QString(QChar::Null)})
            QTest::newRow(qPrintable(QString::number(s.size()) + s.toUtf8().toHex())) << s;
    }
    void invalidNamespace()
    {
        QFETCH(QString, name);
        QTemporaryDir root;
        auto c = config(root);
        c.applicationNamespace = name;
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::InvalidConfig);
        QVERIFY(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void rootMustBePrivate()
    {
        QTemporaryDir root;
        auto c = config(root);
        QVERIFY(::chmod(QFile::encodeName(root.path()).constData(), 0755) == 0);
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::UnsafePath);
        QVERIFY(!QFileInfo::exists(spacePath(c)));
    }
    void namespaceSymlinkAndUnsafeMode()
    {
        QTemporaryDir root, target;
        auto c = config(root);
        QVERIFY(::symlink(QFile::encodeName(target.path()).constData(),
                          QFile::encodeName(spacePath(c)).constData()) == 0);
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::UnsafePath);
        QVERIFY(QFileInfo(spacePath(c)).isSymLink());
        QVERIFY(QFile::remove(spacePath(c)));
        QVERIFY(QDir().mkdir(spacePath(c)));
        QVERIFY(::chmod(QFile::encodeName(spacePath(c)).constData(), 0755) == 0);
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::UnsafePath);
        QVERIFY(QFileInfo::exists(spacePath(c)));
    }
    void restrictiveUmaskRollsBackOwnedNamespace()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        const auto previous = ::umask(0777);
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        ::umask(previous);
        QVERIFY(!endpoint);
        QCOMPARE(error, LinuxEndpointError::UnsafePath);
        QVERIFY(!QFileInfo::exists(spacePath(c)));
    }
    void existingLeafNeverRemoved()
    {
        QTemporaryDir root;
        auto c = config(root);
        QVERIFY(QDir().mkdir(spacePath(c)));
        QVERIFY(::chmod(QFile::encodeName(spacePath(c)).constData(), 0700) == 0);
        QVERIFY(QDir().mkdir(leafPath(c)));
        QVERIFY(writeFile(leafPath(c) + "/marker"));
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::Collision);
        QVERIFY(QFileInfo::exists(leafPath(c) + "/marker"));
    }
    void longVisiblePathFailsAndRollsBack()
    {
        QTemporaryDir root;
        auto c = config(root);
        c.applicationNamespace = QString(64, 'a');
        LinuxEndpointError error{};
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::PathLimit);
        QVERIFY(!QFileInfo::exists(spacePath(c)));
    }
    void nativePeerAndManifest()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        QCOMPARE(error, LinuxEndpointError::None);
        struct stat s{};
        QVERIFY(::lstat(QFile::encodeName(endpoint->socketPath()).constData(), &s) == 0);
        QVERIFY(S_ISSOCK(s.st_mode));
        QCOMPARE(s.st_mode & 07777, 0600u);
        QCOMPARE(s.st_uid, ::geteuid());
        QVERIFY(::lstat(QFile::encodeName(endpoint->manifestPath()).constData(), &s) == 0);
        QVERIFY(S_ISREG(s.st_mode));
        QCOMPARE(s.st_mode & 07777, 0600u);
        QVERIFY(::lstat(QFile::encodeName(leafPath(c)).constData(), &s) == 0);
        QCOMPARE(s.st_mode & 07777, 0700u);
        QVERIFY((::fcntl(endpoint->listeningDescriptor(), F_GETFL) & O_NONBLOCK) != 0);
        QVERIFY((::fcntl(endpoint->listeningDescriptor(), F_GETFD) & FD_CLOEXEC) != 0);
        QFile manifest(endpoint->manifestPath());
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto body = manifest.readAll();
        QVERIFY(body.size() < 4096);
        const auto object = QJsonDocument::fromJson(body).object();
        QCOMPARE(object.size(), 4);
        QCOMPARE(object.value("schema").toInt(), 1);
        QCOMPARE(object.value("instanceId").toString(),
                 c.instanceId.toString(QUuid::WithoutBraces));
        QCOMPARE(object.value("endpoint").toString(), endpoint->socketPath());
        QCOMPARE(endpoint->acceptSameUser(), -1);
        QVERIFY(!linuxSameUserPeer(endpoint->listeningDescriptor()));
        TestFd client{dial(endpoint->socketPath())};
        QVERIFY(client.fd >= 0);
        QVERIFY(linuxSameUserPeer(client.fd));
        TestFd peer{endpoint->acceptSameUser()};
        QVERIFY(peer.fd >= 0);
        QVERIFY(linuxSameUserPeer(peer.fd));
        QVERIFY((::fcntl(peer.fd, F_GETFL) & O_NONBLOCK) != 0);
        QVERIFY((::fcntl(peer.fd, F_GETFD) & FD_CLOEXEC) != 0);
        QVERIFY(::send(client.fd, "synth", 5, MSG_NOSIGNAL) == 5);
        char payload[5]{};
        QCOMPARE(::recv(peer.fd, payload, 5, 0), ssize_t(5));
        QCOMPARE(QByteArray(payload, 5), QByteArray("synth"));
        endpoint->close();
        endpoint->close();
        QCOMPARE(endpoint->listeningDescriptor(), -1);
        QVERIFY(endpoint->socketPath().isEmpty());
        QVERIFY(!QFileInfo::exists(spacePath(c)));
    }
    void collisionDoesNotAffectLiveEndpoint()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        QVERIFY(!LinuxObservationEndpoint::create(c, error));
        QCOMPARE(error, LinuxEndpointError::Collision);
        QVERIFY(QFileInfo::exists(endpoint->manifestPath()));
        TestFd client{dial(endpoint->socketPath())};
        QVERIFY(client.fd >= 0);
        TestFd peer{endpoint->acceptSameUser()};
        QVERIFY(peer.fd >= 0);
    }
    void nativeSameUserAcrossProcess()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = QFile::encodeName(endpoint->socketPath());
        QVERIFY(path.size() < qsizetype(sizeof(address.sun_path)));
        std::memcpy(address.sun_path, path.constData(), size_t(path.size()) + 1);
        int pipes[2]{};
        QVERIFY(::pipe2(pipes, O_CLOEXEC) == 0);
        TestFd reader{pipes[0]}, writer{pipes[1]};
        const auto child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            // After fork, child uses only async-signal-safe native operations; no Qt.
            ::close(reader.fd);
            const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0 ||
                ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
                !linuxSameUserPeer(fd) || ::send(fd, "child", 5, MSG_NOSIGNAL) != 5)
                ::_exit(2);
            if (::write(writer.fd, "y", 1) != 1)
                ::_exit(3);
            ::_exit(0);
        }
        ::close(writer.fd);
        writer.fd = -1;
        pollfd ready{reader.fd, POLLIN, 0};
        const auto polled = ::poll(&ready, 1, 2000);
        if (polled <= 0)
            ::kill(child, SIGKILL); // only this bounded synthetic test child
        int status = 0;
        const auto waited = ::waitpid(child, &status, 0);
        QCOMPARE(waited, child);
        QVERIFY(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        QVERIFY(polled > 0 && (ready.revents & POLLIN));
        char signal{};
        QCOMPARE(::read(reader.fd, &signal, 1), ssize_t(1));
        QCOMPARE(signal, 'y');
        TestFd peer{endpoint->acceptSameUser()};
        QVERIFY(peer.fd >= 0);
        QVERIFY(linuxSameUserPeer(peer.fd));
        ucred credentials{};
        socklen_t size = sizeof(credentials);
        QVERIFY(::getsockopt(peer.fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0);
        QCOMPARE(credentials.pid, child);
        char payload[5]{};
        QCOMPARE(::recv(peer.fd, payload, 5, 0), ssize_t(5));
        QCOMPARE(QByteArray(payload, 5), QByteArray("child"));
    }
    void independentInstances()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto first = LinuxObservationEndpoint::create(c, error);
        QVERIFY(first);
        c.instanceId = QUuid::createUuid();
        auto second = LinuxObservationEndpoint::create(c, error);
        QVERIFY(second);
        const auto secondSocket = second->socketPath();
        first->close();
        QVERIFY(QFileInfo::exists(secondSocket));
        TestFd client{dial(secondSocket)};
        QVERIFY(client.fd >= 0);
        TestFd peer{second->acceptSameUser()};
        QVERIFY(peer.fd >= 0);
        second->close();
        // Shared namespace may remain empty: only its creator could remove it.
        QVERIFY(QDir(spacePath(c)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void replacedManifestAndUnknownFilesRemain()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        const auto path = endpoint->manifestPath();
        QVERIFY(QFile::remove(path));
        QVERIFY(writeFile(path));
        QVERIFY(writeFile(leafPath(c) + "/unknown"));
        endpoint->close();
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(QFileInfo::exists(leafPath(c) + "/unknown"));
        QVERIFY(!QFileInfo::exists(leafPath(c) + "/s"));
    }
    void replacedPublicLeafDoesNotRedirectCleanup()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        const auto old = leafPath(c);
        const auto moved = old + "-moved";
        QVERIFY(QDir().rename(old, moved));
        QVERIFY(QDir().mkdir(old));
        QVERIFY(writeFile(old + "/s"));
        QCOMPARE(endpoint->acceptSameUser(), -1);
        endpoint->close();
        QVERIFY(QFileInfo::exists(old + "/s"));
        QVERIFY(!QFileInfo::exists(moved + "/s"));
        QVERIFY(!QFileInfo::exists(moved + "/observation-v1.json"));
    }
    void permissionRetirement_data()
    {
        QTest::addColumn<int>("node");
        QTest::newRow("root") << 0;
        QTest::newRow("namespace") << 1;
        QTest::newRow("instance") << 2;
        QTest::newRow("socket") << 3;
        QTest::newRow("manifest") << 4;
    }
    void permissionRetirement()
    {
        QFETCH(int, node);
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        TestFd client{dial(endpoint->socketPath())};
        QVERIFY(client.fd >= 0);
        const auto path = node == 0   ? root.path()
                          : node == 1 ? spacePath(c)
                          : node == 2 ? leafPath(c)
                          : node == 3 ? endpoint->socketPath()
                                      : endpoint->manifestPath();
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0755) == 0);
        QCOMPARE(endpoint->acceptSameUser(), -1);
        QVERIFY(::chmod(QFile::encodeName(path).constData(), node < 3 ? 0700 : 0600) == 0);
        TestFd peer{endpoint->acceptSameUser()};
        QVERIFY(peer.fd >= 0);
    }
    void replacedSocketRemains()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        const auto path = endpoint->socketPath();
        TestFd client{dial(path)};
        QVERIFY(client.fd >= 0);
        QVERIFY(QFile::remove(path));
        QVERIFY(writeFile(path));
        QCOMPARE(endpoint->acceptSameUser(), -1);
        endpoint->close();
        QVERIFY(QFileInfo::exists(path));
    }
    void failedCreationsDoNotLeakDescriptors()
    {
        QTemporaryDir root;
        auto c = config(root);
        LinuxEndpointError error{};
        auto endpoint = LinuxObservationEndpoint::create(c, error);
        QVERIFY(endpoint);
        const auto count =
            QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        for (int i = 0; i < 32; ++i) {
            QVERIFY(!LinuxObservationEndpoint::create(c, error));
            QCOMPARE(error, LinuxEndpointError::Collision);
        }
        QCOMPARE(QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(),
                 count);
    }
    void invalidAndNonStreamPeers()
    {
        QVERIFY(!linuxSameUserPeer(-1));
        TestFd file{::open("/dev/null", O_RDONLY | O_CLOEXEC)};
        QVERIFY(file.fd >= 0);
        QVERIFY(!linuxSameUserPeer(file.fd));
        int fds[2]{};
        QVERIFY(::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, fds) == 0);
        TestFd a{fds[0]}, b{fds[1]};
        QVERIFY(!linuxSameUserPeer(a.fd));
        QVERIFY(!linuxSameUserPeer(b.fd));
    }
};
QTEST_APPLESS_MAIN(LinuxObservationEndpointTest)
#include "linux_observation_endpoint_test.moc"
