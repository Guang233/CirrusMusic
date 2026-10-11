#include "linux_observation_endpoint.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

namespace cirrus::infrastructure::ipc
{
namespace
{
struct Fd {
    Fd() = default;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int value = -1;
    ~Fd() { reset(); }
    void reset(int next = -1)
    {
        if (value >= 0)
            ::close(value);
        value = next;
    }
};
bool sameNode(const struct stat& a, const struct stat& b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
           (a.st_mode & S_IFMT) == (b.st_mode & S_IFMT);
}
bool privateDirectory(int fd)
{
    struct stat s{};
    return fd >= 0 && ::fstat(fd, &s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == ::geteuid() &&
           (s.st_mode & 07777) == 0700;
}
bool atMatches(int parent, const QByteArray& name, const struct stat& identity)
{
    struct stat s{};
    return ::fstatat(parent, name.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
           sameNode(s, identity);
}
bool namespaceValid(const QString& name)
{
    if (name.isEmpty() || name.size() > 64 || name == "." || name == "..")
        return false;
    for (QChar c : name) {
        const auto u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
              u == '.' || u == '_' || u == '-'))
            return false;
    }
    return true;
}
bool bindPath(int fd, const QByteArray& path)
{
    sockaddr_un address{};
    if (path.isEmpty() || path.size() >= qsizetype(sizeof(address.sun_path)))
        return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()) + 1);
    return ::bind(fd, reinterpret_cast<const sockaddr*>(&address),
                  socklen_t(offsetof(sockaddr_un, sun_path) + size_t(path.size()) + 1)) == 0;
}
} // namespace
bool linuxSameUserPeer(int descriptor)
{
    if (::getuid() != ::geteuid())
        return false; // no setuid deployment contract
    sockaddr_un address{};
    socklen_t addressSize = sizeof(address);
    int type = 0;
    socklen_t typeSize = sizeof(type);
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    return ::getpeername(descriptor, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0 &&
           address.sun_family == AF_UNIX &&
           ::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &typeSize) == 0 &&
           typeSize == sizeof(type) && type == SOCK_STREAM &&
           ::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 &&
           size == sizeof(credentials) && credentials.pid > 0 && credentials.uid == ::geteuid();
}
struct LinuxObservationEndpoint::Impl {
    Fd root, space, leaf, listener, manifest;
    QByteArray spaceName, leafName;
    QString rootPath, publicSocket, publicManifest;
    struct stat spaceIdentity{}, leafIdentity{}, socketIdentity{}, manifestIdentity{};
    bool ownSpace = false, ownLeaf = false, ownSocket = false, ownManifest = false,
         ownTemporary = false;
    bool privateNode(const QByteArray& name, const struct stat& identity) const
    {
        struct stat s{};
        return ::fstatat(leaf.value, name.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
               sameNode(s, identity) && s.st_uid == ::geteuid() && (s.st_mode & 07777) == 0600;
    }
    bool pathCurrent() const
    {
        Fd current;
        current.value = ::open(QFile::encodeName(rootPath).constData(),
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat a{}, b{};
        return privateDirectory(current.value) && privateDirectory(space.value) &&
               privateDirectory(leaf.value) && ::fstat(root.value, &a) == 0 &&
               ::fstat(current.value, &b) == 0 && sameNode(a, b) &&
               atMatches(root.value, spaceName, spaceIdentity) &&
               atMatches(space.value, leafName, leafIdentity) &&
               (!ownSocket || privateNode("s", socketIdentity)) &&
               (!ownManifest || privateNode("observation-v1.json", manifestIdentity));
    }
    void clean()
    {
        // Pinned directories prevent a replaced public path from redirecting unlink.
        // Same-user malware/root are outside the protection guarantee. Unknown files remain.
        if (ownManifest && atMatches(leaf.value, "observation-v1.json", manifestIdentity))
            ::unlinkat(leaf.value, "observation-v1.json", 0);
        if (ownTemporary && atMatches(leaf.value, "manifest.tmp", manifestIdentity))
            ::unlinkat(leaf.value, "manifest.tmp", 0);
        if (ownSocket && atMatches(leaf.value, "s", socketIdentity))
            ::unlinkat(leaf.value, "s", 0);
        ownManifest = ownTemporary = ownSocket = false;
        manifest.reset();
        listener.reset();
        if (ownLeaf && atMatches(space.value, leafName, leafIdentity))
            ::unlinkat(space.value, leafName.constData(), AT_REMOVEDIR);
        if (ownSpace && atMatches(root.value, spaceName, spaceIdentity))
            ::unlinkat(root.value, spaceName.constData(), AT_REMOVEDIR);
        ownLeaf = ownSpace = false;
        leaf.reset();
        space.reset();
        root.reset();
    }
    ~Impl() { clean(); }
};
LinuxObservationEndpoint::LinuxObservationEndpoint() : m_impl(std::make_unique<Impl>()) {}
LinuxObservationEndpoint::~LinuxObservationEndpoint() = default;
std::unique_ptr<LinuxObservationEndpoint>
LinuxObservationEndpoint::create(const LinuxObservationEndpointConfig& config,
                                 LinuxEndpointError& error)
{
    error = LinuxEndpointError::Disabled;
    if (!config.enabled)
        return {};
    error = LinuxEndpointError::InvalidConfig;
    if (!namespaceValid(config.applicationNamespace) || config.instanceId.isNull() ||
        !config.runtimeRoot.isValidUtf16() || config.runtimeRoot.contains(QChar::Null) ||
        !QFileInfo(config.runtimeRoot).isAbsolute() || ::getuid() != ::geteuid())
        return {};
    auto result = std::unique_ptr<LinuxObservationEndpoint>(new LinuxObservationEndpoint);
    auto& p = *result->m_impl;
    // Canonicalization is only for the configured OS runtime root. Private descendants
    // are traversed with no-follow and pinned descriptors; never canonicalize their links.
    p.rootPath = QFileInfo(config.runtimeRoot).canonicalFilePath();
    error = LinuxEndpointError::UnsafePath;
    if (p.rootPath.isEmpty())
        return {};
    p.root.value = ::open(QFile::encodeName(p.rootPath).constData(),
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!privateDirectory(p.root.value))
        return {};
    p.spaceName = config.applicationNamespace.toLatin1();
    const int createdSpace = ::mkdirat(p.root.value, p.spaceName.constData(), 0700);
    if (createdSpace != 0 && errno != EEXIST) {
        error = LinuxEndpointError::Io;
        return {};
    }
    if (::fstatat(p.root.value, p.spaceName.constData(), &p.spaceIdentity, AT_SYMLINK_NOFOLLOW) !=
        0)
        return {};
    p.ownSpace = createdSpace == 0;
    p.space.value = ::openat(p.root.value, p.spaceName.constData(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat openedSpace{};
    if (::fstat(p.space.value, &openedSpace) != 0 || !sameNode(openedSpace, p.spaceIdentity) ||
        !privateDirectory(p.space.value))
        return {};
    p.leafName = "i-" + config.instanceId.toString(QUuid::WithoutBraces).toLatin1();
    if (::mkdirat(p.space.value, p.leafName.constData(), 0700) != 0) {
        error = errno == EEXIST ? LinuxEndpointError::Collision : LinuxEndpointError::Io;
        return {};
    }
    if (::fstatat(p.space.value, p.leafName.constData(), &p.leafIdentity, AT_SYMLINK_NOFOLLOW) != 0)
        return {};
    p.ownLeaf = true;
    p.leaf.value = ::openat(p.space.value, p.leafName.constData(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat openedLeaf{};
    if (::fstat(p.leaf.value, &openedLeaf) != 0 || !sameNode(openedLeaf, p.leafIdentity) ||
        !privateDirectory(p.leaf.value))
        return {};
    const QString base =
        p.rootPath + '/' + config.applicationNamespace + '/' + QString::fromLatin1(p.leafName);
    p.publicSocket = base + "/s";
    p.publicManifest = base + "/observation-v1.json";
    sockaddr_un sizing{};
    if (QFile::encodeName(p.publicSocket).size() >= qsizetype(sizeof(sizing.sun_path))) {
        error = LinuxEndpointError::PathLimit;
        return {};
    }
    error = LinuxEndpointError::Io;
    p.listener.value = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    // Linux /proc/self/fd is a kernel-owned alias to the pinned private directory.
    // If unavailable, fail closed. The advertised client path is the normal pathname.
    const QByteArray pinnedPath = "/proc/self/fd/" + QByteArray::number(p.leaf.value) + "/s";
    if (p.listener.value < 0 || !bindPath(p.listener.value, pinnedPath) ||
        ::fstatat(p.leaf.value, "s", &p.socketIdentity, AT_SYMLINK_NOFOLLOW) != 0)
        return {};
    p.ownSocket = true;
    struct stat securedSocket{};
    if (!S_ISSOCK(p.socketIdentity.st_mode) || p.socketIdentity.st_uid != ::geteuid() ||
        ::fchmodat(p.leaf.value, "s", 0600, AT_SYMLINK_NOFOLLOW) != 0 ||
        ::fstatat(p.leaf.value, "s", &securedSocket, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameNode(securedSocket, p.socketIdentity) || (securedSocket.st_mode & 07777) != 0600 ||
        ::listen(p.listener.value, 8) != 0 || !p.pathCurrent())
        return {};
    p.manifest.value = ::openat(p.leaf.value, "manifest.tmp",
                                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (p.manifest.value < 0 || ::fstat(p.manifest.value, &p.manifestIdentity) != 0)
        return {};
    p.ownTemporary = true;
    if (::fchmod(p.manifest.value, 0600) != 0)
        return {};
    const QByteArray body =
        QJsonDocument(QJsonObject{{"schema", 1},
                                  {"instanceId", config.instanceId.toString(QUuid::WithoutBraces)},
                                  {"endpoint", p.publicSocket},
                                  {"pid", qint64(::getpid())}})
            .toJson(QJsonDocument::Compact);
    if (body.size() > 4096)
        return {};
    qsizetype written = 0;
    unsigned attempts = 0;
    while (written < body.size() && attempts++ < 16) {
        const auto n =
            ::write(p.manifest.value, body.constData() + written, size_t(body.size() - written));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return {};
        written += n;
    }
    if (written != body.size())
        return {};
    if (!p.pathCurrent() || ::syscall(SYS_renameat2, p.leaf.value, "manifest.tmp", p.leaf.value,
                                      "observation-v1.json", RENAME_NOREPLACE) != 0)
        return {};
    p.ownTemporary = false;
    p.ownManifest = true;
    error = LinuxEndpointError::None;
    return result;
}
QString LinuxObservationEndpoint::manifestPath() const
{
    return m_impl->ownManifest ? m_impl->publicManifest : QString{};
}
QString LinuxObservationEndpoint::socketPath() const
{
    return m_impl->ownSocket ? m_impl->publicSocket : QString{};
}
int LinuxObservationEndpoint::listeningDescriptor() const { return m_impl->listener.value; }
int LinuxObservationEndpoint::acceptSameUser()
{
    if (!m_impl->pathCurrent())
        return -1;
    const int fd =
        ::accept4(m_impl->listener.value, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0 && !linuxSameUserPeer(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
}
bool LinuxObservationEndpoint::current() const
{
    return m_impl->listener.value >= 0 && m_impl->pathCurrent();
}
void LinuxObservationEndpoint::close() { m_impl->clean(); }
} // namespace cirrus::infrastructure::ipc
