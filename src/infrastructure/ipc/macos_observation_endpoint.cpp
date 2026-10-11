#if defined(__APPLE__) && defined(__MACH__)
#include "local_observation_endpoint.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/acl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace cirrus::infrastructure::ipc
{
namespace
{
struct Fd {
    int value = -1;
    Fd() = default;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }
    void reset()
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
bool matches(int parent, const QByteArray& name, const struct stat& identity)
{
    struct stat s{};
    return ::fstatat(parent, name.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 && same(s, identity);
}
// Darwin extended ALLOW ACL entries can grant rights beyond Unix mode bits.
// Keep this boundary conservative: absent/empty or DENY-only ACLs, bounded scan.
// acl_get_entry uses Darwin's numeric-index extension, returning 0 for success
// and EINVAL for an index past a VALID ACL (not POSIX iteration's 1/0 convention).
bool denyOnlyAcl(acl_t acl, int error)
{
    struct Release {
        acl_t value;
        ~Release()
        {
            if (value)
                ::acl_free(value);
        }
    } release{acl};
    if (!acl)
        return error == ENOENT;
    if (::acl_valid(acl) != 0)
        return false;
    for (int index = 0; index < 128; ++index) {
        acl_entry_t entry{};
        errno = 0;
        if (::acl_get_entry(acl, index, &entry) != 0)
            return errno == EINVAL;
        acl_tag_t tag{};
        if (::acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY)
            return false;
    }
    return false;
}
bool aclSafe(int fd)
{
    errno = 0;
    const auto acl = ::acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
    const int error = errno;
    return denyOnlyAcl(acl, error);
}
bool aclSafe(const QString& path)
{
    errno = 0;
    const auto acl = ::acl_get_link_np(QFile::encodeName(path).constData(), ACL_TYPE_EXTENDED);
    const int error = errno;
    return denyOnlyAcl(acl, error);
}
bool privateDirectory(int fd)
{
    struct stat s{};
    return fd >= 0 && ::fstat(fd, &s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == ::geteuid() &&
           (s.st_mode & 07777) == 0700 && aclSafe(fd);
}
bool namespaceValid(const QString& name)
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
bool flags(int fd)
{
    const int f = ::fcntl(fd, F_GETFL), d = ::fcntl(fd, F_GETFD), yes = 1;
    return f >= 0 && d >= 0 && ::fcntl(fd, F_SETFL, f | O_NONBLOCK) == 0 &&
           ::fcntl(fd, F_SETFD, d | FD_CLOEXEC) == 0 &&
           ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) == 0;
}
// Darwin has no Linux /proc/self/fd bind alias. An ordinary bind path is only
// admissible through a trusted ancestor chain; other users must not rename it.
// World-writable ancestors are allowed ONLY root-owned sticky directories and
// every child in that chain must be root/current-user owned. No fchdir/umask.
bool pinAncestors(const QString& path, std::vector<std::unique_ptr<Fd>>& pins)
{
    QString current = "/";
    auto parts = path.split('/', Qt::SkipEmptyParts);
    parts.prepend(QString{});
    for (const auto& part : parts) {
        if (!part.isEmpty())
            current = QDir(current).filePath(part);
        auto fd = std::make_unique<Fd>();
        fd->value = ::open(QFile::encodeName(current).constData(),
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat s{};
        if (fd->value < 0 || ::fstat(fd->value, &s) != 0 || !S_ISDIR(s.st_mode) ||
            (s.st_uid != 0 && s.st_uid != ::geteuid()))
            return false;
        if ((s.st_mode & 0022) != 0 && !(s.st_uid == 0 && (s.st_mode & S_ISVTX)))
            return false;
        if (!aclSafe(fd->value))
            return false;
        pins.push_back(std::move(fd));
    }
    return true;
}
class MacEndpoint final : public LocalObservationEndpoint
{
  public:
    ~MacEndpoint() override { close(); }
    QString manifestPath() const override { return publicManifest; }
    QString address() const override { return publicSocket; }
    LocalObservationHandle readiness() const override
    {
        return listener.value < 0
                   ? LocalObservationHandle{}
                   : LocalObservationHandle{listener.value, LocalObservationHandleKind::Socket};
    }
    bool current() const override
    {
        Fd visible;
        visible.value = ::open(QFile::encodeName(rootPath).constData(),
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat a{}, b{};
        return privateDirectory(root.value) && privateDirectory(visible.value) &&
               privateDirectory(space.value) && privateDirectory(leaf.value) &&
               ::fstat(root.value, &a) == 0 && ::fstat(visible.value, &b) == 0 && same(a, b) &&
               matches(root.value, spaceName, spaceIdentity) &&
               matches(space.value, leafName, leafIdentity) &&
               (!ownSocket || privateNode("s", socketIdentity)) &&
               (!ownManifest || privateNode("observation-v1.json", manifestIdentity));
    }
    LocalObservationAdmission acceptSameUser() override
    {
        if (listener.value < 0 || !current())
            return {};
        const int fd = ::accept(listener.value, nullptr, nullptr);
        if (fd < 0)
            return {};
        LocalObservationHandle peer{fd, LocalObservationHandleKind::Socket};
        if (!flags(fd) || !sameUserLocalObservationPeer(peer, LocalObservationPeerRole::Server) ||
            !current()) {
            ::close(fd);
            return {};
        }
        return {peer, 0, false};
    }
    void close() override
    {
        listener.reset();
        if (ownManifest && matches(leaf.value, "observation-v1.json", manifestIdentity))
            ::unlinkat(leaf.value, "observation-v1.json", 0);
        if (ownTemporary && matches(leaf.value, "manifest.tmp", manifestIdentity))
            ::unlinkat(leaf.value, "manifest.tmp", 0);
        if (ownSocket && matches(leaf.value, "s", socketIdentity))
            ::unlinkat(leaf.value, "s", 0);
        ownManifest = ownTemporary = ownSocket = false;
        manifest.reset();
        if (ownLeaf && matches(space.value, leafName, leafIdentity))
            ::unlinkat(space.value, leafName.constData(), AT_REMOVEDIR);
        if (ownSpace && matches(root.value, spaceName, spaceIdentity))
            ::unlinkat(root.value, spaceName.constData(), AT_REMOVEDIR);
        ownLeaf = ownSpace = false;
        leaf.reset();
        space.reset();
        root.reset();
        ancestors.clear();
    }
    bool privateNode(const QByteArray& name, const struct stat& identity) const
    {
        struct stat s{};
        return ::fstatat(leaf.value, name.constData(), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
               same(s, identity) && s.st_uid == ::geteuid() && (s.st_mode & 07777) == 0600 &&
               aclSafe(QFileInfo(publicSocket).path() + '/' + QString::fromLatin1(name));
    }
    Fd root, space, leaf, listener, manifest;
    std::vector<std::unique_ptr<Fd>> ancestors;
    QString rootPath, publicSocket, publicManifest;
    QByteArray spaceName, leafName;
    struct stat spaceIdentity{}, leafIdentity{}, socketIdentity{}, manifestIdentity{};
    bool ownSpace = false, ownLeaf = false, ownSocket = false, ownTemporary = false,
         ownManifest = false;
};
} // namespace
bool macObservationSameUserPeer(qintptr descriptor)
{
    if (descriptor < 0 || descriptor > INT_MAX || ::getuid() != ::geteuid())
        return false;
    const int fd = int(descriptor);
    sockaddr_un address{};
    socklen_t size = sizeof(address);
    int type = 0;
    socklen_t typeSize = sizeof(type);
    uid_t uid = uid_t(-1);
    gid_t gid = gid_t(-1);
    return ::getpeername(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0 &&
           address.sun_family == AF_UNIX &&
           ::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &typeSize) == 0 &&
           typeSize == sizeof(type) && type == SOCK_STREAM && ::getpeereid(fd, &uid, &gid) == 0 &&
           uid == ::geteuid();
}
std::unique_ptr<LocalObservationEndpoint>
createMacObservationEndpoint(const LocalObservationEndpointConfig& config,
                             LocalEndpointError& error)
{
    error = LocalEndpointError::InvalidConfig;
    if (!config.enabled) {
        error = LocalEndpointError::Disabled;
        return {};
    }
    if (!namespaceValid(config.applicationNamespace) || config.instanceId.isNull() ||
        !config.runtimeRoot.isValidUtf16() || config.runtimeRoot.contains(QChar::Null) ||
        config.runtimeRoot.size() > 1024 || !QFileInfo(config.runtimeRoot).isAbsolute() ||
        ::getuid() != ::geteuid())
        return {};
    auto endpoint = std::make_unique<MacEndpoint>();
    auto& p = *endpoint;
    p.rootPath = QFileInfo(config.runtimeRoot).canonicalFilePath();
    error = LocalEndpointError::UnsafePath;
    if (p.rootPath.isEmpty() || !pinAncestors(p.rootPath, p.ancestors))
        return {};
    p.root.value = ::open(QFile::encodeName(p.rootPath).constData(),
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct statfs filesystem{};
    if (!privateDirectory(p.root.value) || ::fstatfs(p.root.value, &filesystem) != 0 ||
        !(filesystem.f_flags & MNT_LOCAL))
        return {};
    p.spaceName = config.applicationNamespace.toLatin1();
    const int madeSpace = ::mkdirat(p.root.value, p.spaceName.constData(), 0700);
    if (madeSpace != 0 && errno != EEXIST) {
        error = LocalEndpointError::Io;
        return {};
    }
    if (::fstatat(p.root.value, p.spaceName.constData(), &p.spaceIdentity, AT_SYMLINK_NOFOLLOW) !=
        0)
        return {};
    p.ownSpace = madeSpace == 0;
    p.space.value = ::openat(p.root.value, p.spaceName.constData(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat opened{};
    if (!privateDirectory(p.space.value) || ::fstat(p.space.value, &opened) != 0 ||
        !same(opened, p.spaceIdentity))
        return {};
    p.leafName = "i-" + config.instanceId.toString(QUuid::WithoutBraces).toLatin1();
    if (::mkdirat(p.space.value, p.leafName.constData(), 0700) != 0) {
        error = errno == EEXIST ? LocalEndpointError::Collision : LocalEndpointError::Io;
        return {};
    }
    if (::fstatat(p.space.value, p.leafName.constData(), &p.leafIdentity, AT_SYMLINK_NOFOLLOW) != 0)
        return {};
    p.ownLeaf = true;
    p.leaf.value = ::openat(p.space.value, p.leafName.constData(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!privateDirectory(p.leaf.value) || ::fstat(p.leaf.value, &opened) != 0 ||
        !same(opened, p.leafIdentity))
        return {};
    const auto base =
        p.rootPath + '/' + config.applicationNamespace + '/' + QString::fromLatin1(p.leafName);
    p.publicSocket = base + "/s";
    p.publicManifest = base + "/observation-v1.json";
    const auto path = QFile::encodeName(p.publicSocket);
    sockaddr_un address{};
    if (path.size() >= qsizetype(sizeof(address.sun_path))) {
        error = LocalEndpointError::PathLimit;
        return {};
    }
    p.listener.value = ::socket(AF_UNIX, SOCK_STREAM, 0);
    error = LocalEndpointError::Io;
    if (p.listener.value < 0 || !flags(p.listener.value) || !p.current())
        return {};
    address.sun_family = AF_UNIX;
    address.sun_len = static_cast<decltype(address.sun_len)>(offsetof(sockaddr_un, sun_path) +
                                                             size_t(path.size()) + 1);
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()) + 1);
    if (::bind(p.listener.value, reinterpret_cast<sockaddr*>(&address), address.sun_len) != 0)
        return {};
    if (::fstatat(p.leaf.value, "s", &p.socketIdentity, AT_SYMLINK_NOFOLLOW) != 0)
        return {};
    p.ownSocket = true;
    if (!S_ISSOCK(p.socketIdentity.st_mode) || p.socketIdentity.st_uid != ::geteuid() ||
        ::fchmodat(p.leaf.value, "s", 0600, AT_SYMLINK_NOFOLLOW) != 0 || !p.current() ||
        ::listen(p.listener.value, 8) != 0)
        return {};
    p.manifest.value = ::openat(p.leaf.value, "manifest.tmp",
                                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (p.manifest.value < 0 || ::fstat(p.manifest.value, &p.manifestIdentity) != 0)
        return {};
    p.ownTemporary = true;
    if (::fchmod(p.manifest.value, 0600) != 0 || !aclSafe(p.manifest.value))
        return {};
    const auto body =
        QJsonDocument(QJsonObject{{"schema", 1},
                                  {"instanceId", config.instanceId.toString(QUuid::WithoutBraces)},
                                  {"endpoint", p.publicSocket},
                                  {"pid", qint64(::getpid())}})
            .toJson(QJsonDocument::Compact);
    if (body.size() > 4096)
        return {};
    qsizetype written = 0;
    for (int i = 0; i < 16 && written < body.size(); ++i) {
        const auto n =
            ::write(p.manifest.value, body.constData() + written, size_t(body.size() - written));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return {};
        written += n;
    }
    if (written != body.size() || !p.current() ||
        !matches(p.leaf.value, "manifest.tmp", p.manifestIdentity))
        return {};
    // Atomic no-replace publication without Linux renameat2. Private source is a
    // pinned, owned regular file; unknown destination is never replaced.
    if (::linkat(p.leaf.value, "manifest.tmp", p.leaf.value, "observation-v1.json", 0) != 0) {
        error = errno == EEXIST ? LocalEndpointError::Collision : LocalEndpointError::Io;
        return {};
    }
    p.ownManifest = true;
    if (::unlinkat(p.leaf.value, "manifest.tmp", 0) != 0)
        return {};
    p.ownTemporary = false;
    if (!p.current())
        return {};
    error = LocalEndpointError::None;
    return endpoint;
}
} // namespace cirrus::infrastructure::ipc
#endif
