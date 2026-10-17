#include "fonts.h"

#include <QFontDatabase>
#include <QLoggingCategory>

namespace cirrus::ui
{
namespace
{
Q_LOGGING_CATEGORY(lcFonts, "cirrus.ui.fonts")

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

// Text font decision point (UQ-03). Text currently uses the platform UI font,
// which also provides the platform's own CJK and other-script fallback. To ship
// a text font later, add it to RESOURCES and register it here instead.
QString textFamily() { return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(); }

struct Registered {
    QString sans;
    QString symbols;
};

const Registered& registered()
{
    static const Registered fonts{
        textFamily(),
        registerFont(QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/MaterialSymbolsRounded.ttf")),
    };
    return fonts;
}
} // namespace

Fonts::Fonts(QObject* parent) : QObject(parent) { registered(); }

QString Fonts::sans() const { return registered().sans; }

QString Fonts::symbols() const { return registered().symbols; }

} // namespace cirrus::ui
