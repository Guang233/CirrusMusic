#include "formatter.h"

#include <QLocale>

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
