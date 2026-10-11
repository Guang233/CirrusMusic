#if defined(_WIN32)
#include "local_observation_endpoint.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <aclapi.h>
#include <cstddef>
#include <cstring>
#include <qt_windows.h>
#include <sddl.h>
#include <vector>

namespace cirrus::infrastructure::ipc
{
bool windowsObservationSameUserPeer(qintptr, LocalObservationPeerRole);
namespace
{
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    bool valid() const { return value != nullptr && value != INVALID_HANDLE_VALUE; }
    void reset()
    {
        if (valid())
            ::CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};
struct LocalAllocation {
    void* value = nullptr;
    ~LocalAllocation()
    {
        if (value)
            ::LocalFree(value);
    }
};
const wchar_t* wide(const QString& text) { return reinterpret_cast<const wchar_t*>(text.utf16()); }
bool notImpersonating()
{
    Handle token;
    if (::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &token.value))
        return false;
    return ::GetLastError() == ERROR_NO_TOKEN;
}
struct UserIdentity {
    std::vector<BYTE> storage;
    DWORD session = 0;
    PSID sid() const
    {
        return storage.empty() ? nullptr
                               : reinterpret_cast<const TOKEN_USER*>(storage.data())->User.Sid;
    }
    bool read(HANDLE token)
    {
        DWORD n = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &n);
        if (!n || n > 16384)
            return false;
        storage.resize(n);
        DWORD actual = 0;
        if (!::GetTokenInformation(token, TokenUser, storage.data(), n, &actual) || actual > n ||
            !::IsValidSid(sid()))
            return false;
        DWORD size = 0;
        return ::GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size) &&
               size == sizeof(session);
    }
    bool current()
    {
        Handle token;
        return notImpersonating() &&
               ::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value) &&
               read(token.value);
    }
};
bool onlyUser(HANDLE object, const UserIdentity& user)
{
    PSID owner = nullptr;
    PACL acl = nullptr;
    LocalAllocation sd;
    if (::GetSecurityInfo(object, SE_FILE_OBJECT,
                          OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                          &acl, nullptr,
                          reinterpret_cast<PSECURITY_DESCRIPTOR*>(&sd.value)) != ERROR_SUCCESS ||
        !owner || !acl || !::EqualSid(owner, user.sid()))
        return false;
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!::GetSecurityDescriptorControl(sd.value, &control, &revision) ||
        !(control & SE_DACL_PROTECTED) || !acl->AceCount)
        return false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* ace = nullptr;
        if (!::GetAce(acl, i, &ace))
            return false;
        auto* header = static_cast<ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERITED_ACE) ||
            header->AceSize < sizeof(ACCESS_ALLOWED_ACE))
            return false;
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        auto* sid = reinterpret_cast<PSID>(&allowed->SidStart);
        if (!::IsValidSid(sid) || !::EqualSid(sid, user.sid()) || !allowed->Mask)
            return false;
    }
    return true;
}
struct PrivateSecurity {
    LocalAllocation sd;
    SECURITY_ATTRIBUTES attributes{};
    bool init(const UserIdentity& user)
    {
        LocalAllocation text;
        if (!::ConvertSidToStringSidW(user.sid(), reinterpret_cast<LPWSTR*>(&text.value)))
            return false;
        const auto sid = QString::fromWCharArray(static_cast<LPCWSTR>(text.value));
        const auto sddl = "O:" + sid + "D:P(A;;FA;;;" + sid + ")";
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                wide(sddl), SDDL_REVISION_1, reinterpret_cast<PSECURITY_DESCRIPTOR*>(&sd.value),
                nullptr))
            return false;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = sd.value;
        attributes.bInheritHandle = FALSE;
        return true;
    }
};
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
bool identity(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info, bool directory)
{
    return ::GetFileInformationByHandle(handle, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}
bool same(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
{
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
std::unique_ptr<Handle> openNode(const QString& path, bool directory, bool deletable = false)
{
    auto h = std::make_unique<Handle>();
    h->value = ::CreateFileW(
        wide(QDir::toNativeSeparators(path)),
        FILE_READ_ATTRIBUTES | READ_CONTROL | (deletable ? DELETE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!h->valid() || !identity(h->value, info, directory))
        return {};
    return h;
}
bool pathMatches(const QString& path, HANDLE pinned, bool directory, const UserIdentity& user)
{
    auto current = openNode(path, directory);
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    return current && identity(pinned, a, directory) && identity(current->value, b, directory) &&
           same(a, b) && onlyUser(pinned, user) && onlyUser(current->value, user);
}
void deleteOwned(HANDLE handle)
{
    FILE_DISPOSITION_INFO info{};
    info.DeleteFile = TRUE;
    ::SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info));
}
struct PendingPipe {
    Handle pipe, event;
    OVERLAPPED connect{}, probe{};
    BYTE firstByte = 0;
    bool outstanding = false, connected = false;
    bool readStarted = false, readOutstanding = false, readDone = false;
    qint64 connectedAt = -1;
    ~PendingPipe()
    {
        if (outstanding && pipe.valid()) {
            ::CancelIoEx(pipe.value, &connect);
            DWORD ignored = 0;
            ::GetOverlappedResult(pipe.value, &connect, &ignored, TRUE);
        }
        if (readOutstanding && pipe.valid()) {
            ::CancelIoEx(pipe.value, &probe);
            DWORD ignored = 0;
            ::GetOverlappedResult(pipe.value, &probe, &ignored, TRUE);
        }
        // Cancellation completion may wait: resource cleanup MUST be on I/O,
        // never playback owner. OVERLAPPED/event storage stays live until then.
        if (pipe.valid())
            ::DisconnectNamedPipe(pipe.value);
    }
};
class WindowsEndpoint final : public LocalObservationEndpoint
{
  public:
    ~WindowsEndpoint() override { close(); }
    QString manifestPath() const override { return publicManifest; }
    QString address() const override { return pipeName; }
    LocalObservationHandle readiness() const override
    {
        return pending && pending->event.valid()
                   ? LocalObservationHandle{reinterpret_cast<qintptr>(pending->event.value),
                                            LocalObservationHandleKind::Event}
                   : LocalObservationHandle{};
    }
    bool current() const override
    {
        return notImpersonating() && root && space && leaf && manifest && pending &&
               pending->pipe.valid() && pathMatches(rootPath, root->value, true, user) &&
               pathMatches(spacePath, space->value, true, user) &&
               pathMatches(leafPath, leaf->value, true, user) &&
               pathMatches(publicManifest, manifest->value, false, user) &&
               onlyUser(pending->pipe.value, user);
    }
    std::unique_ptr<PendingPipe> newPipe(bool first)
    {
        auto p = std::make_unique<PendingPipe>();
        p->event.value = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!p->event.valid())
            return {};
        p->connect.hEvent = p->event.value;
        p->pipe.value = ::CreateNamedPipeW(
            wide(pipeName),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 9, 65536,
            4096, 0, &security.attributes);
        if (!p->pipe.valid() || !onlyUser(p->pipe.value, user))
            return {};
        if (::ConnectNamedPipe(p->pipe.value, &p->connect)) {
            p->connected = true;
            ::SetEvent(p->event.value);
        } else {
            const DWORD error = ::GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                p->connected = true;
                ::SetEvent(p->event.value);
            } else if (error == ERROR_IO_PENDING)
                p->outstanding = true;
            else
                return {};
        }
        return p;
    }
    LocalObservationAdmission acceptSameUser() override
    {
        if (!current())
            return {};
        if (pending->outstanding) {
            DWORD n = 0;
            if (!::GetOverlappedResult(pending->pipe.value, &pending->connect, &n, FALSE)) {
                if (::GetLastError() == ERROR_IO_INCOMPLETE)
                    return {};
                close();
                return {};
            }
            pending->outstanding = false;
            pending->connected = true;
        }
        if (!pending->connected)
            return {};
        if (pending->connectedAt < 0)
            pending->connectedAt = clock.elapsed();
        bool readable = clock.elapsed() - pending->connectedAt < 5000;
        if (readable && !pending->readStarted) {
            pending->readStarted = true;
            pending->probe.hEvent = pending->event.value;
            if (!::ResetEvent(pending->event.value))
                readable = false;
            else {
                DWORD n = 0;
                if (::ReadFile(pending->pipe.value, &pending->firstByte, 1, nullptr,
                               &pending->probe))
                    pending->readDone =
                        ::GetOverlappedResult(pending->pipe.value, &pending->probe, &n, FALSE) &&
                        n == 1;
                else if (::GetLastError() == ERROR_IO_PENDING)
                    pending->readOutstanding = true;
                else
                    readable = false;
            }
        }
        if (readable && pending->readOutstanding) {
            DWORD n = 0;
            if (::GetOverlappedResult(pending->pipe.value, &pending->probe, &n, FALSE)) {
                pending->readOutstanding = false;
                pending->readDone = n == 1;
            } else if (::GetLastError() == ERROR_IO_INCOMPLETE)
                return {};
            else {
                pending->readOutstanding = false;
                readable = false;
            }
        }
        // Peek is not a read and does not establish the last-read message's
        // impersonation context. Preserve the consumed byte for the caller.
        const bool admitted =
            readable && pending->readDone &&
            windowsObservationSameUserPeer(reinterpret_cast<qintptr>(pending->pipe.value),
                                           LocalObservationPeerRole::Server) &&
            current();
        // Keep one server instance alive while creating its replacement. This
        // preserves the FIRST_PIPE_INSTANCE namespace claim across every accept.
        // A replacement failure is terminal; never close then blindly take over.
        auto replacement = newPipe(false);
        if (!replacement) {
            close();
            return {};
        }
        auto old = std::move(pending);
        pending = std::move(replacement);
        if (!admitted)
            return {};
        HANDLE result = old->pipe.value;
        old->pipe.value = INVALID_HANDLE_VALUE;
        return {{reinterpret_cast<qintptr>(result), LocalObservationHandleKind::Pipe},
                old->firstByte,
                true};
    }
    void close() override
    {
        pending.reset();
        if (manifest && ownManifest)
            deleteOwned(manifest->value);
        manifest.reset();
        ownManifest = false;
        if (leaf && ownLeaf)
            deleteOwned(leaf->value);
        leaf.reset();
        ownLeaf = false;
        if (space && ownSpace)
            deleteOwned(space->value);
        space.reset();
        ownSpace = false;
        root.reset();
        ancestors.clear();
    }
    UserIdentity user;
    PrivateSecurity security;
    std::unique_ptr<Handle> root, space, leaf, manifest;
    std::vector<std::unique_ptr<Handle>> ancestors;
    std::unique_ptr<PendingPipe> pending;
    QString rootPath, spacePath, leafPath, publicManifest, pipeName;
    QElapsedTimer clock;
    bool ownSpace = false, ownLeaf = false, ownManifest = false;
};
} // namespace
bool windowsObservationSameUserPeer(qintptr native, LocalObservationPeerRole role)
{
    if (native == 0 || native == -1)
        return false;
    HANDLE pipe = reinterpret_cast<HANDLE>(native);
    UserIdentity self;
    if (!self.current())
        return false;
    DWORD flags = 0;
    if (!::GetNamedPipeInfo(pipe, &flags, nullptr, nullptr, nullptr))
        return false;
    if (role == LocalObservationPeerRole::Client) {
        if ((flags & PIPE_SERVER_END) || !onlyUser(pipe, self))
            return false;
        ULONG pid = 0;
        if (!::GetNamedPipeServerProcessId(pipe, &pid) || !pid)
            return false;
        Handle process, token;
        process.value = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        UserIdentity server;
        ULONG again = 0;
        DWORD exit = 0;
        return process.valid() && ::GetExitCodeProcess(process.value, &exit) &&
               exit == STILL_ACTIVE &&
               ::OpenProcessToken(process.value, TOKEN_QUERY, &token.value) &&
               server.read(token.value) && ::EqualSid(self.sid(), server.sid()) &&
               self.session == server.session && ::GetNamedPipeServerProcessId(pipe, &again) &&
               again == pid && onlyUser(pipe, self);
    }
    if (!(flags & PIPE_SERVER_END) || !onlyUser(pipe, self))
        return false;
    // Caller must have completed a read on this connection, not merely Peek.
    if (!::ImpersonateNamedPipeClient(pipe))
        return false;
    bool allowed = false;
    {
        Handle token;
        UserIdentity peer;
        allowed = ::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &token.value) &&
                  peer.read(token.value) && ::EqualSid(self.sid(), peer.sid()) &&
                  self.session == peer.session;
    }
    if (!::RevertToSelf())
        qFatal("Observation pipe cannot restore native security context");
    return allowed;
}
void closeWindowsObservationPeer(qintptr native, LocalObservationPeerRole role)
{
    if (native == 0 || native == -1)
        return;
    HANDLE pipe = reinterpret_cast<HANDLE>(native);
    if (role == LocalObservationPeerRole::Server)
        ::DisconnectNamedPipe(pipe);
    ::CloseHandle(pipe); // Caller MUST first complete/cancel its own overlapped I/O.
}
std::unique_ptr<LocalObservationEndpoint>
createWindowsObservationEndpoint(const LocalObservationEndpointConfig& config,
                                 LocalEndpointError& error)
{
    error = LocalEndpointError::InvalidConfig;
    if (!config.enabled) {
        error = LocalEndpointError::Disabled;
        return {};
    }
    const auto configured = QDir::fromNativeSeparators(config.runtimeRoot);
    if (!namespaceValid(config.applicationNamespace) || config.instanceId.isNull() ||
        !configured.isValidUtf16() || configured.contains(QChar::Null) ||
        configured.size() > 1024 || configured.size() < 3 || !configured[0].isLetter() ||
        configured[0].unicode() > 127 || configured[1] != ':' || configured[2] != '/')
        return {};
    if (::GetDriveTypeW(wide(QDir::toNativeSeparators(configured.left(3)))) != DRIVE_FIXED) {
        error = LocalEndpointError::UnsafePath;
        return {};
    }
    auto endpoint = std::make_unique<WindowsEndpoint>();
    auto& p = *endpoint;
    if (!p.user.current() || !p.security.init(p.user))
        return {};
    p.rootPath = QDir::fromNativeSeparators(QFileInfo(configured).canonicalFilePath());
    error = LocalEndpointError::UnsafePath;
    if (p.rootPath.size() < 3 || p.rootPath[1] != ':' || p.rootPath[2] != '/')
        return {};
    // Pin every ancestor without FILE_SHARE_DELETE: no directory rename/reparse
    // can redirect later pathname operations. Never use UNC/network roots.
    QString cursor = p.rootPath.left(3);
    auto pin = openNode(cursor, true);
    if (!pin)
        return {};
    p.ancestors.push_back(std::move(pin));
    for (const auto& part : p.rootPath.mid(3).split('/', Qt::SkipEmptyParts)) {
        cursor = QDir(cursor).filePath(part);
        pin = openNode(cursor, true);
        if (!pin)
            return {};
        p.ancestors.push_back(std::move(pin));
    }
    p.root = openNode(p.rootPath, true);
    if (!p.root || !onlyUser(p.root->value, p.user))
        return {};
    p.spacePath = QDir(p.rootPath).filePath(config.applicationNamespace);
    if (::CreateDirectoryW(wide(QDir::toNativeSeparators(p.spacePath)), &p.security.attributes))
        p.ownSpace = true;
    else if (::GetLastError() != ERROR_ALREADY_EXISTS) {
        error = LocalEndpointError::Io;
        return {};
    }
    p.space = openNode(p.spacePath, true, true);
    if (!p.space || !onlyUser(p.space->value, p.user))
        return {};
    p.leafPath =
        QDir(p.spacePath).filePath("i-" + config.instanceId.toString(QUuid::WithoutBraces));
    if (!::CreateDirectoryW(wide(QDir::toNativeSeparators(p.leafPath)), &p.security.attributes)) {
        error = ::GetLastError() == ERROR_ALREADY_EXISTS ? LocalEndpointError::Collision
                                                         : LocalEndpointError::Io;
        return {};
    }
    p.ownLeaf = true;
    p.leaf = openNode(p.leafPath, true, true);
    if (!p.leaf || !onlyUser(p.leaf->value, p.user))
        return {};
    const auto id = config.instanceId.toString(QUuid::WithoutBraces);
    p.pipeName = "\\\\.\\pipe\\" + config.applicationNamespace + ".observation." + id;
    p.publicManifest = QDir(p.leafPath).filePath("observation-v1.json");
    error = LocalEndpointError::Io;
    p.clock.start();
    p.pending = p.newPipe(true);
    if (!p.pending) {
        error = ::GetLastError() == ERROR_ACCESS_DENIED ? LocalEndpointError::Collision
                                                        : LocalEndpointError::Io;
        return {};
    }
    const auto temporary = QDir(p.leafPath).filePath("manifest.tmp");
    p.manifest = std::make_unique<Handle>();
    p.manifest->value = ::CreateFileW(
        wide(QDir::toNativeSeparators(temporary)), GENERIC_WRITE | READ_CONTROL | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &p.security.attributes, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!p.manifest->valid())
        return {};
    p.ownManifest = true;
    BY_HANDLE_FILE_INFORMATION manifestInfo{};
    if (!identity(p.manifest->value, manifestInfo, false) || !onlyUser(p.manifest->value, p.user))
        return {};
    const auto body = QJsonDocument(QJsonObject{{"schema", 1},
                                                {"instanceId", id},
                                                {"endpoint", p.pipeName},
                                                {"pid", qint64(::GetCurrentProcessId())}})
                          .toJson(QJsonDocument::Compact);
    if (body.size() > 4096)
        return {};
    DWORD written = 0;
    if (!::WriteFile(p.manifest->value, body.constData(), DWORD(body.size()), &written, nullptr) ||
        written != DWORD(body.size()))
        return {};
    const auto destination = QDir::toNativeSeparators(p.publicManifest);
    const size_t nameBytes = size_t(destination.size()) * sizeof(wchar_t);
    const size_t bytes = offsetof(FILE_RENAME_INFO, FileName) + nameBytes;
    std::vector<BYTE> storage(bytes);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = DWORD(nameBytes);
    std::memcpy(rename->FileName, destination.utf16(), nameBytes);
    if (!pathMatches(p.rootPath, p.root->value, true, p.user) ||
        !pathMatches(p.spacePath, p.space->value, true, p.user) ||
        !pathMatches(p.leafPath, p.leaf->value, true, p.user) ||
        !::SetFileInformationByHandle(p.manifest->value, FileRenameInfo, rename, DWORD(bytes)) ||
        !p.current())
        return {};
    error = LocalEndpointError::None;
    return endpoint;
}
} // namespace cirrus::infrastructure::ipc
#endif
