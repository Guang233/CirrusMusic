#include "linux_observation_client.h"
#include "linux_observation_endpoint.h"
#include "playback_observation_codec.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace cirrus::infrastructure::ipc
{
namespace
{
struct Fd {
    int value = -1;
    ~Fd()
    {
        if (value >= 0)
            ::close(value);
    }
    void close()
    {
        if (value >= 0)
            ::close(value);
        value = -1;
    }
};
bool same(const struct stat& a, const struct stat& b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
           (a.st_mode & S_IFMT) == (b.st_mode & S_IFMT);
}
bool directory(int fd)
{
    struct stat s{};
    return fd >= 0 && ::fstat(fd, &s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == ::geteuid() &&
           (s.st_mode & 07777) == 0700;
}
bool privateAt(int parent, const QByteArray& name, const struct stat& identity, mode_t type,
               mode_t mode)
{
    struct stat s{};
    return ::fstatat(parent, name.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 && same(s, identity) &&
           (s.st_mode & S_IFMT) == type && s.st_uid == ::geteuid() && (s.st_mode & 07777) == mode;
}
bool spaceValid(const QString& name)
{
    if (name.isEmpty() || name.size() > 64 || name == "." || name == "..")
        return false;
    for (QChar c : name) {
        auto u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
              u == '.' || u == '_' || u == '-'))
            return false;
    }
    return true;
}
bool integer(const QJsonValue& v, qint64 low, qint64 high)
{
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= low &&
           v.toDouble() <= high && std::floor(v.toDouble()) == v.toDouble();
}
} // namespace
class LinuxObservationClient::Impl
{
  public:
    bool current() const
    {
        Fd check;
        check.value = ::open(QFile::encodeName(rootPath).constData(),
                             O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
        struct stat s{};
        return directory(check.value) && ::fstat(check.value, &s) == 0 && same(s, rootIdentity) &&
               directory(root.value) && directory(space.value) && directory(leaf.value) &&
               privateAt(root.value, spaceName, spaceIdentity, S_IFDIR, 0700) &&
               privateAt(space.value, leafName, leafIdentity, S_IFDIR, 0700) &&
               privateAt(leaf.value, "observation-v1.json", manifestIdentity, S_IFREG, 0600) &&
               privateAt(leaf.value, "s", socketIdentity, S_IFSOCK, 0600);
    }
    LinuxObservationClientState fail(LinuxObservationClientError e)
    {
        error = e;
        state = LinuxObservationClientState::Failed;
        socket.close();
        return state;
    }
    bool hello(const QJsonObject& o) const
    {
        if (o["v"] != QJsonValue(1) || o["kind"] != QJsonValue("response") ||
            o["id"] != QJsonValue("hello") || o["ok"] != QJsonValue(true))
            return false;
        const auto id = o["instanceId"].toString();
        if (id != instance.toString(QUuid::WithoutBraces))
            return false;
        const QJsonArray expectedMethods{"hello", "getSnapshot", "subscribe", "unsubscribe",
                                         "getLyricsSlice"};
        QJsonArray tracks;
        for (const auto& t : PlaybackObservationCodec::trackNames())
            tracks.append(t);
        if (o["methods"] != expectedMethods || o["tracks"] != tracks || !o["limits"].isObject())
            return false;
        const auto limits = o["limits"].toObject();
        const QJsonObject expected{{"requestBytes", 4096},    {"frameBytes", 1048576},
                                   {"snapshotBytes", 262144}, {"documentBytes", 8388608},
                                   {"maxLines", 128},         {"clients", 8},
                                   {"pendingRequests", 4},    {"framesPerSecond", 16},
                                   {"burstFrames", 16},       {"helloMs", 5000},
                                   {"partialFrameMs", 2000},  {"inputBytes", 16384},
                                   {"clientBytes", 2097152},  {"allClientBytes", 16777216},
                                   {"slowMs", 5000}};
        for (auto it = expected.begin(); it != expected.end(); ++it)
            if (limits[it.key()] != it.value())
                return false;
        return true;
    }
    Fd root, space, leaf, manifest, socket;
    struct stat rootIdentity{}, spaceIdentity{}, leafIdentity{}, manifestIdentity{},
        socketIdentity{};
    QString rootPath;
    QByteArray spaceName, leafName, input, output;
    QUuid instance;
    QElapsedTimer clock;
    qsizetype sent = 0;
    quint32 length = 0;
    LinuxObservationClientState state = LinuxObservationClientState::Connecting;
    LinuxObservationClientError error = LinuxObservationClientError::None;
};
LinuxObservationClient::LinuxObservationClient() : m_impl(std::make_unique<Impl>()) {}
LinuxObservationClient::~LinuxObservationClient() = default;
std::unique_ptr<LinuxObservationClient>
LinuxObservationClient::connect(const LinuxObservationClientConfig& config,
                                LinuxObservationClientError& error)
{
    error = LinuxObservationClientError::InvalidConfig;
    if (::getuid() != ::geteuid() || !spaceValid(config.applicationNamespace) ||
        config.instanceId.isNull() || !config.runtimeRoot.isValidUtf16() ||
        config.runtimeRoot.contains(QChar::Null) || !QFileInfo(config.runtimeRoot).isAbsolute())
        return {};
    auto client = std::unique_ptr<LinuxObservationClient>(new LinuxObservationClient);
    auto& p = *client->m_impl;
    p.instance = config.instanceId;
    p.rootPath = QFileInfo(config.runtimeRoot).canonicalFilePath();
    p.spaceName = config.applicationNamespace.toLatin1();
    p.leafName = "i-" + config.instanceId.toString(QUuid::WithoutBraces).toLatin1();
    error = LinuxObservationClientError::UnsafePath;
    p.root.value = ::open(QFile::encodeName(p.rootPath).constData(),
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!directory(p.root.value) || ::fstat(p.root.value, &p.rootIdentity) != 0)
        return {};
    p.space.value = ::openat(p.root.value, p.spaceName.constData(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    p.leaf.value = ::openat(p.space.value, p.leafName.constData(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!directory(p.space.value) || !directory(p.leaf.value) ||
        ::fstat(p.space.value, &p.spaceIdentity) != 0 ||
        ::fstat(p.leaf.value, &p.leafIdentity) != 0)
        return {};
    p.manifest.value = ::openat(p.leaf.value, "observation-v1.json",
                                O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (p.manifest.value < 0 || ::fstat(p.manifest.value, &p.manifestIdentity) != 0 ||
        !S_ISREG(p.manifestIdentity.st_mode) || p.manifestIdentity.st_size <= 0 ||
        p.manifestIdentity.st_size > 4096 ||
        ::fstatat(p.leaf.value, "s", &p.socketIdentity, AT_SYMLINK_NOFOLLOW) != 0 || !p.current())
        return {};
    QByteArray body(qsizetype(p.manifestIdentity.st_size), Qt::Uninitialized);
    qsizetype n = 0;
    unsigned attempts = 0;
    while (n < body.size() && attempts++ < 16) {
        auto count = ::read(p.manifest.value, body.data() + n, size_t(body.size() - n));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return {};
        n += count;
    }
    char extra;
    if (n != body.size() || ::read(p.manifest.value, &extra, 1) != 0)
        return {};
    const auto parsed = PlaybackObservationCodec::strictLocalObject(body);
    error = LinuxObservationClientError::Protocol;
    if (!parsed)
        return {};
    const auto o = *parsed;
    const auto keys = o.keys();
    if (QSet<QString>(keys.begin(), keys.end()) !=
            QSet<QString>{"schema", "instanceId", "endpoint", "pid"} ||
        o["schema"] != QJsonValue(1) || !integer(o["pid"], 1, 2147483647))
        return {};
    if (o["instanceId"] != QJsonValue(config.instanceId.toString(QUuid::WithoutBraces))) {
        error = LinuxObservationClientError::InstanceMismatch;
        return {};
    }
    const auto expected = p.rootPath + '/' + config.applicationNamespace + '/' +
                          QString::fromLatin1(p.leafName) + "/s";
    if (o["endpoint"] != QJsonValue(expected))
        return {};
    const auto path = "/proc/self/fd/" + QByteArray::number(p.leaf.value) + "/s";
    sockaddr_un address{};
    if (QFile::encodeName(expected).size() >= qsizetype(sizeof(address.sun_path)) ||
        path.size() >= qsizetype(sizeof(address.sun_path)))
        return {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()) + 1);
    p.socket.value = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    error = LinuxObservationClientError::Unavailable;
    if (p.socket.value < 0 || !p.current())
        return {};
    const auto connected =
        ::connect(p.socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connected != 0 && errno != EINPROGRESS)
        return {};
    p.output = PlaybackObservationCodec::frame(
        QByteArrayView("{\"v\":1,\"kind\":\"request\",\"id\":\"hello\",\"method\":\"hello\"}"));
    p.clock.start();
    error = LinuxObservationClientError::None;
    return client;
}
LinuxObservationClientState LinuxObservationClient::advance()
{
    auto& p = *m_impl;
    if (p.state == LinuxObservationClientState::Failed ||
        p.state == LinuxObservationClientState::Closed ||
        p.state == LinuxObservationClientState::Verified)
        return p.state;
    if (p.clock.elapsed() >= 5000)
        return p.fail(LinuxObservationClientError::Timeout);
    if (!p.current())
        return p.fail(LinuxObservationClientError::UnsafePath);
    if (p.state == LinuxObservationClientState::Connecting) {
        pollfd poller{p.socket.value, POLLOUT, 0};
        const auto ready = ::poll(&poller, 1, 0);
        if (ready < 0 && errno == EINTR)
            return p.state;
        if (ready < 0)
            return p.fail(LinuxObservationClientError::Unavailable);
        if (!ready)
            return p.state;
        int socketError = 0;
        socklen_t size = sizeof(socketError);
        if (::getsockopt(p.socket.value, SOL_SOCKET, SO_ERROR, &socketError, &size) != 0 ||
            size != sizeof(socketError) || socketError || !linuxSameUserPeer(p.socket.value))
            return p.fail(LinuxObservationClientError::Unavailable);
        p.state = LinuxObservationClientState::Hello;
    }
    if (p.sent < p.output.size()) {
        const auto n = ::send(p.socket.value, p.output.constData() + p.sent,
                              size_t(p.output.size() - p.sent), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return p.state;
        if (n <= 0)
            return p.fail(LinuxObservationClientError::Unavailable);
        p.sent += n;
    }
    std::array<char, 1024> bytes{};
    const auto target = p.length ? qsizetype(p.length) + 4 : 4;
    const auto n =
        ::recv(p.socket.value, bytes.data(),
               size_t(std::min(qsizetype(bytes.size()), target - p.input.size())), MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return p.state;
    if (n <= 0)
        return p.fail(LinuxObservationClientError::Unavailable);
    p.input.append(bytes.data(), n);
    if (p.input.size() == 4 && !p.length) {
        for (int i = 0; i < 4; ++i)
            p.length = (p.length << 8) | uchar(p.input[i]);
        if (!p.length || p.length > 4096)
            return p.fail(LinuxObservationClientError::Protocol);
        return p.state;
    }
    if (p.input.size() == qsizetype(p.length) + 4) {
        const auto parsed =
            PlaybackObservationCodec::strictLocalObject(QByteArrayView(p.input).sliced(4));
        if (!parsed)
            return p.fail(LinuxObservationClientError::Protocol);
        if ((*parsed)["instanceId"] != QJsonValue(p.instance.toString(QUuid::WithoutBraces)))
            return p.fail(LinuxObservationClientError::InstanceMismatch);
        if (!p.hello(*parsed) || !p.current() || !linuxSameUserPeer(p.socket.value))
            return p.fail(LinuxObservationClientError::Protocol);
        p.state = LinuxObservationClientState::Verified;
        p.input.clear();
        p.output.clear();
    }
    return p.state;
}
LinuxObservationClientState LinuxObservationClient::state() const { return m_impl->state; }
LinuxObservationClientError LinuxObservationClient::error() const { return m_impl->error; }
int LinuxObservationClient::takeVerifiedDescriptor()
{
    auto& p = *m_impl;
    if (p.state != LinuxObservationClientState::Verified)
        return -1;
    if (!p.current() || !linuxSameUserPeer(p.socket.value)) {
        p.fail(LinuxObservationClientError::UnsafePath);
        return -1;
    }
    const int fd = p.socket.value;
    p.socket.value = -1;
    p.state = LinuxObservationClientState::Closed;
    return fd;
}
void LinuxObservationClient::close()
{
    m_impl->socket.close();
    m_impl->state = LinuxObservationClientState::Closed;
}
} // namespace cirrus::infrastructure::ipc
