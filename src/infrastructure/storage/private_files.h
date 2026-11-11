#pragma once

#include <QFile>
#include <QString>

#include <memory>

namespace cirrus::storage
{

// Directories and files the caches keep (resource cache, audio cache): ours alone and
// never reached through a link. On Linux and macOS: owned by us, 0700/0600, one link,
// no symlink, and no ancestor that someone else owns or can write (group/world-writable);
// the process must not run set-uid/set-gid. On Windows: owned by our user (or the token's
// default owner, Administrators when elevated), no reparse point, one link, and no ACE that
// lets Everyone, Authenticated Users, Users, Interactive or Anonymous write it. Windows
// ancestors are not checked; the profile's default ACL keeps %LOCALAPPDATA% private.

// `dir` is absolute, clean and a private directory.
[[nodiscard]] bool isPrivateDirectory(const QString& dir);
// Creates `dir` (not its parents) owner-only when missing, then checks it.
[[nodiscard]] bool makePrivateDirectory(const QString& dir);
// `path` does not exist, or is a private regular file.
[[nodiscard]] bool isPrivateFileOrAbsent(const QString& path);
// Opens `path` read-write, creating it owner-only, never through a link; null unless the
// opened file is private. On Windows others may read, write and delete it meanwhile, as
// on POSIX (readers use their own handles).
[[nodiscard]] std::unique_ptr<QFile> openPrivateFile(const QString& path);

} // namespace cirrus::storage
