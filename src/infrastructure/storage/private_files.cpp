#include "private_files.h"

#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <qt_windows.h>

#include <aclapi.h>
#include <fcntl.h>
#include <io.h>

#include <string>
#include <vector>
#elif defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cirrus::storage
{
namespace
{
bool absoluteAndClean(const QString& path)
{
    return !path.isEmpty() && QDir::isAbsolutePath(path) && QDir::cleanPath(path) == path;
}

#if defined(Q_OS_WIN)
std::wstring native(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }

std::vector<char> tokenInformation(TOKEN_INFORMATION_CLASS kind)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, kind, nullptr, 0, &size);
    std::vector<char> buffer(size);
    if (size == 0 || !GetTokenInformation(token, kind, buffer.data(), size, &size))
        buffer.clear();
    CloseHandle(token);
    return buffer;
}

// Our user, or the owner the token gives new objects (Administrators when elevated).
bool ours(PSID sid)
{
    const auto user = tokenInformation(TokenUser);
    if (!user.empty() && EqualSid(sid, reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid))
        return true;
    const auto owner = tokenInformation(TokenOwner);
    return !owner.empty() &&
           EqualSid(sid, reinterpret_cast<const TOKEN_OWNER*>(owner.data())->Owner);
}

bool broad(PSID sid)
{
    for (const auto type : {WinWorldSid, WinAuthenticatedUserSid, WinBuiltinUsersSid,
                            WinInteractiveSid, WinAnonymousSid}) {
        BYTE buffer[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(buffer);
        if (CreateWellKnownSid(type, nullptr, buffer, &size) && EqualSid(sid, buffer))
            return true;
    }
    return false;
}

// The DACL lets a broad group change the object or what is in it.
bool writableByOthers(PACL dacl)
{
    constexpr ACCESS_MASK Write = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                                  FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE | WRITE_DAC |
                                  WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
    ACL_SIZE_INFORMATION size{};
    if (!GetAclInformation(dacl, &size, sizeof(size), AclSizeInformation))
        return true;
    for (DWORD i = 0; i < size.AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace))
            return true;
        const auto* header = static_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE))
            continue;
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        if ((allowed->Mask & Write) && broad(&allowed->SidStart))
            return true;
    }
    return false;
}

bool privateHandle(HANDLE handle, bool directory)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (!directory && info.nNumberOfLinks != 1))
        return false;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    // A null DACL grants everyone everything.
    const bool good = owner && ours(owner) && dacl && !writableByOthers(dacl);
    LocalFree(descriptor);
    return good;
}

// Opens the object itself, not what a reparse point (symlink, junction) leads to.
HANDLE openForCheck(const QString& path, bool directory)
{
    return CreateFileW(
        native(path).c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
}
#elif defined(Q_OS_UNIX)
bool privateFileInfo(const struct stat& info)
{
    return S_ISREG(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & 0777) == 0600 &&
           info.st_nlink == 1;
}
#endif
} // namespace

#if defined(Q_OS_WIN)

bool isPrivateDirectory(const QString& dir)
{
    if (!absoluteAndClean(dir))
        return false;
    const HANDLE handle = openForCheck(dir, true);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const bool good = privateHandle(handle, true);
    CloseHandle(handle);
    return good;
}

bool makePrivateDirectory(const QString& dir)
{
    // The new directory inherits its parent's ACL; isPrivateDirectory() checks the result.
    if (absoluteAndClean(dir) && !CreateDirectoryW(native(dir).c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return isPrivateDirectory(dir);
}

bool isPrivateFileOrAbsent(const QString& path)
{
    const HANDLE handle = openForCheck(path, false);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    const bool good = privateHandle(handle, false);
    CloseHandle(handle);
    return good;
}

std::unique_ptr<QFile> openPrivateFile(const QString& path)
{
    const HANDLE handle =
        CreateFileW(native(path).c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    if (!privateHandle(handle, false)) {
        CloseHandle(handle);
        return {};
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        return {};
    }
    auto file = std::make_unique<QFile>(path);
    if (!file->open(fd, QIODevice::ReadWrite, QFileDevice::AutoCloseHandle)) {
        _close(fd);
        return {};
    }
    return file;
}

#elif defined(Q_OS_UNIX)

bool isPrivateDirectory(const QString& dir)
{
    if (getuid() != geteuid() || getgid() != getegid() || !absoluteAndClean(dir))
        return false;
    struct stat info{};
    auto bytes = QFile::encodeName(dir);
    if (lstat(bytes.constData(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0777) != 0700)
        return false;
    auto ancestor = QFileInfo(dir).absolutePath();
    while (true) {
        bytes = QFile::encodeName(ancestor);
        if (lstat(bytes.constData(), &info) || !S_ISDIR(info.st_mode) ||
            (info.st_uid != 0 && info.st_uid != geteuid()) || (info.st_mode & 0022))
            return false;
        if (ancestor == QStringLiteral("/"))
            break;
        ancestor = QFileInfo(ancestor).absolutePath();
    }
    return true;
}

bool makePrivateDirectory(const QString& dir)
{
    if (absoluteAndClean(dir) && !QFileInfo::exists(dir)) {
        const auto bytes = QFile::encodeName(dir);
        if (::mkdir(bytes.constData(), 0700) != 0 && errno != EEXIST)
            return false;
    }
    return isPrivateDirectory(dir);
}

bool isPrivateFileOrAbsent(const QString& path)
{
    struct stat info{};
    const auto bytes = QFile::encodeName(path);
    if (lstat(bytes.constData(), &info) == 0)
        return privateFileInfo(info);
    return errno == ENOENT;
}

std::unique_ptr<QFile> openPrivateFile(const QString& path)
{
    const auto bytes = QFile::encodeName(path);
    const int fd = ::open(bytes.constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return {};
    struct stat info{};
    if (fstat(fd, &info) != 0 || !privateFileInfo(info)) {
        ::close(fd);
        return {};
    }
    auto file = std::make_unique<QFile>(path);
    if (!file->open(fd, QIODevice::ReadWrite, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
    return file;
}

#else

bool isPrivateDirectory(const QString&) { return false; }
bool makePrivateDirectory(const QString&) { return false; }
bool isPrivateFileOrAbsent(const QString&) { return false; }
std::unique_ptr<QFile> openPrivateFile(const QString&) { return {}; }

#endif

} // namespace cirrus::storage
