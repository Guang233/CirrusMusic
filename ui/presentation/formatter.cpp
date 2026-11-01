#include "formatter.h"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace cirrus::ui
{

QString Formatter::duration(qint64 milliseconds)
{
    if (milliseconds < 0)
        return QStringLiteral("--:--");
    const qint64 seconds = milliseconds / 1000;
    const auto twoDigits = [](qint64 value) {
        return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
    };
    if (seconds >= 3600)
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg(twoDigits(seconds / 60 % 60), twoDigits(seconds % 60));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(twoDigits(seconds % 60));
}

QString Formatter::date(qint64 msSinceEpoch)
{
    if (msSinceEpoch <= 0)
        return {};
    const QDate day = QDateTime::fromMSecsSinceEpoch(msSinceEpoch).date();
    if (day.year() == QDate::currentDate().year())
        return tr("%1月%2日").arg(day.month()).arg(day.day());
    return tr("%1年%2月%3日").arg(day.year()).arg(day.month()).arg(day.day());
}

QString Formatter::fullDate(qint64 msSinceEpoch)
{
    if (msSinceEpoch <= 0)
        return {};
    const QDate day = QDateTime::fromMSecsSinceEpoch(msSinceEpoch).date();
    return tr("%1年%2月%3日").arg(day.year()).arg(day.month()).arg(day.day());
}

QString Formatter::totalLength(qint64 milliseconds)
{
    // Rounded up, so a few seconds don't read as "0 分钟".
    const qint64 minutes = (std::max<qint64>(milliseconds, 0) + 59'999) / 60'000;
    if (minutes < 60)
        return tr("%1 分钟").arg(minutes);
    return minutes % 60 == 0 ? tr("%1 小时").arg(minutes / 60)
                             : tr("%1 小时 %2 分钟").arg(minutes / 60).arg(minutes % 60);
}

QString Formatter::artists(const QList<ArtistRef>& artists)
{
    QStringList names;
    names.reserve(artists.size());
    for (const ArtistRef& artist : artists)
        names << artist.name;
    return names.join(tr(" / "));
}

QString Formatter::count(qint64 value)
{
    const QLocale locale;
    // Chinese and Japanese group by 10^4 (万, 億/亿); others by 10^3.
    const bool cjk =
        locale.language() == QLocale::Chinese || locale.language() == QLocale::Japanese;
    struct Unit {
        qint64 size;
        QString suffix;
    };
    const QList<Unit> units = cjk ? QList<Unit>{{100'000'000, locale.language() == QLocale::Japanese
                                                                  ? QStringLiteral("億")
                                                                  : QStringLiteral("亿")},
                                                {10'000, QStringLiteral("万")}}
                                  : QList<Unit>{{1'000'000'000, QStringLiteral("B")},
                                                {1'000'000, QStringLiteral("M")},
                                                {1'000, QStringLiteral("K")}};
    for (const Unit& unit : units) {
        if (qAbs(value) >= unit.size) {
            // One decimal below 100 and when not whole: "1.2万", "12K", "120万".
            const double scaled = std::round(double(value) / double(unit.size) * 10) / 10;
            const bool whole = qAbs(scaled) >= 100 || scaled == std::trunc(scaled);
            return locale.toString(scaled, 'f', whole ? 0 : 1) + unit.suffix;
        }
    }
    return locale.toString(value);
}

} // namespace cirrus::ui
