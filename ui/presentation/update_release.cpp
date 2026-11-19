#include "update_release.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace cirrus::ui::updates
{
std::optional<std::array<int, 3>> parseVersion(QStringView text)
{
    if (text.startsWith(u'v'))
        text = text.mid(1);
    const auto parts = text.split(u'.');
    if (parts.size() != 3)
        return std::nullopt;
    std::array<int, 3> version{};
    for (int i = 0; i < 3; ++i) {
        // Digits only: toInt() would take "+1" or " 1".
        if (parts[i].isEmpty() || parts[i].size() > 6)
            return std::nullopt;
        for (QChar c : parts[i])
            if (c < u'0' || c > u'9')
                return std::nullopt;
        version[i] = parts[i].toInt();
    }
    return version;
}

bool isNewer(QStringView latest, QStringView current)
{
    const auto a = parseVersion(latest);
    const auto b = parseVersion(current);
    return a && b && *a > *b;
}

bool isReleasePage(const QUrl& url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host() == QLatin1String("github.com") && url.port() == -1 &&
           url.userInfo().isEmpty() &&
           url.path().startsWith(QLatin1String("/Guang233/CirrusMusic/releases/"));
}

QString notesToPlainText(const QString& markdown)
{
    static const QRegularExpression link(QStringLiteral(R"(!?\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression marks(QStringLiteral(R"(\*\*|__|`)"));
    static const QRegularExpression heading(QStringLiteral(R"(^#{1,6}\s+)"));
    static const QRegularExpression item(QStringLiteral(R"(^(\s*)[-*+]\s+)"));
    static const QRegularExpression tag(QStringLiteral(R"(<[^>]*>)"));
    QStringList out;
    for (QString line : markdown.split(u'\n')) {
        line.remove(u'\r');
        line.replace(link, QStringLiteral("\\1"));
        line.remove(tag);
        line.remove(marks);
        if (line.contains(heading)) {
            line.remove(heading);
        } else if (const auto match = item.match(line); match.hasMatch()) {
            line = match.captured(1) + QStringLiteral("• ") + line.mid(match.capturedEnd());
        }
        out << (line.trimmed().isEmpty() ? QString() : line);
    }
    // At most one empty line in a row, none at the ends.
    QString text = out.join(u'\n');
    static const QRegularExpression blanks(QStringLiteral("\n{3,}"));
    text.replace(blanks, QStringLiteral("\n\n"));
    return text.trimmed();
}

std::optional<Release> parseRelease(const QByteArray& json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.value(QLatin1String("draft")).toBool() ||
        object.value(QLatin1String("prerelease")).toBool())
        return std::nullopt;
    const QString tag = object.value(QLatin1String("tag_name")).toString();
    if (!parseVersion(tag))
        return std::nullopt;
    Release release;
    release.version = tag.startsWith(u'v') ? tag.mid(1) : tag;
    release.title = object.value(QLatin1String("name")).toString();
    if (release.title.isEmpty())
        release.title = QStringLiteral("Cirrus %1").arg(release.version);
    release.notes = notesToPlainText(object.value(QLatin1String("body")).toString());
    release.page = QUrl(object.value(QLatin1String("html_url")).toString());
    release.published =
        QDateTime::fromString(object.value(QLatin1String("published_at")).toString(), Qt::ISODate);
    if (!isReleasePage(release.page))
        return std::nullopt;
    return release;
}
} // namespace cirrus::ui::updates
