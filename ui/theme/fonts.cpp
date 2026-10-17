#include "fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QLoggingCategory>

namespace cirrus::ui
{
namespace
{
Q_LOGGING_CATEGORY(lcFonts, "cirrus.ui.fonts")

// Preferred order per platform; families missing on this machine are ignored by Qt.
const QStringList kCjkFallbacks = {
    QStringLiteral("Noto Sans CJK SC"),    QStringLiteral("Source Han Sans SC"),
    QStringLiteral("Microsoft YaHei UI"),  QStringLiteral("Microsoft YaHei"),
    QStringLiteral("PingFang SC"),         QStringLiteral("Hiragino Sans GB"),
    QStringLiteral("WenQuanYi Micro Hei"),
};

QString registerFont(const QString& path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    const QStringList families =
        id < 0 ? QStringList{} : QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qCWarning(lcFonts) << "failed to register bundled font" << path;
        return {};
    }
    return families.constFirst();
}

struct Registered {
    QString sans;
    QString symbols;
};

const Registered& registered()
{
    static const Registered fonts = [] {
        Registered result{
            registerFont(QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/GoogleSansFlex.ttf")),
            registerFont(QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/MaterialSymbolsRounded.ttf")),
        };
        if (!result.sans.isEmpty())
            QFont::insertSubstitutions(result.sans, kCjkFallbacks);
        return result;
    }();
    return fonts;
}
} // namespace

Fonts::Fonts(QObject* parent) : QObject(parent) { registered(); }

QString Fonts::sans() const { return registered().sans; }

QString Fonts::symbols() const { return registered().symbols; }

QStringList Fonts::cjkFallbacks() const { return kCjkFallbacks; }

QStringList Fonts::installedFallbacks() const
{
    QStringList installed;
    for (const QString& family : kCjkFallbacks) {
        if (QFontDatabase::hasFamily(family))
            installed << family;
    }
    return installed;
}

} // namespace cirrus::ui
