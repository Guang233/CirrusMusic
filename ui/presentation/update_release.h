#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <array>
#include <optional>

class QByteArray;

namespace cirrus::ui::updates
{
// The pure parts of the update check (docs/tasks/update-check.md): no network, so they
// are tested on their own.

// The latest release, as GitHub's "latest release" answer gives it.
struct Release {
    QString version; // "0.1.2", the tag without its "v"
    QString title;   // the release's name ("Cirrus 0.1.2")
    QString notes;   // its body (the CHANGELOG section) as plain text
    QUrl page;       // the release page on github.com
    QDateTime published;
};

// "v1.2.3" or "1.2.3" as {1, 2, 3}; anything else (a pre-release suffix, missing or
// extra parts, letters) is nullopt.
std::optional<std::array<int, 3>> parseVersion(QStringView text);
// Whether `latest` is a newer version than `current`; false when either is not one.
bool isNewer(QStringView latest, QStringView current);
// GitHub's latest-release JSON; nullopt when it is malformed, a draft, a pre-release,
// has no usable version or its page is not the project's (isReleasePage).
std::optional<Release> parseRelease(const QByteArray& json);
// A release page of the project on GitHub (https only): the only page the app opens
// for an update.
bool isReleasePage(const QUrl& url);
// The release notes (GitHub Markdown, the CHANGELOG's style) as plain text for a
// selectable text block: headings as their text, list items as "• ", links as their
// text, emphasis and code marks dropped; no HTML ever reaches the UI.
QString notesToPlainText(const QString& markdown);
} // namespace cirrus::ui::updates
