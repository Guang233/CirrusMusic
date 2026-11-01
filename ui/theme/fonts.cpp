#include "fonts.h"

#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSEngine>
#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <utility>

namespace cirrus::ui
{
namespace
{
Q_LOGGING_CATEGORY(lcFonts, "cirrus.ui.fonts")

const QString kSymbolsPath =
    QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/MaterialSymbolsRounded.ttf");
// Embedded only when tools/ui/prepare_fonts.py fetched it (ui/theme/CMakeLists.txt).
const QString kMiSansPath = QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/MiSansVF.ttf");

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

// CSS weight -> MiSans `wght` (continuous, 150–700), interpolating linearly between its
// named instances: Thin 150, ExtraLight 200, Light 250, Regular 330, Medium 380,
// Semibold 520, Bold 630, Heavy 700.
qreal misansWeight(qreal weight)
{
    static constexpr std::array<std::pair<qreal, qreal>, 9> stops{{
        {100, 150},
        {200, 200},
        {300, 250},
        {400, 330},
        {500, 380},
        {600, 520},
        {700, 630},
        {800, 700},
        {900, 700},
    }};
    weight = std::clamp(weight, qreal(100), qreal(900));
    for (size_t i = 1; i < stops.size(); ++i) {
        const auto [w1, a1] = stops[i];
        if (weight <= w1) {
            const auto [w0, a0] = stops[i - 1];
            return a0 + (a1 - a0) * (weight - w0) / (w1 - w0);
        }
    }
    return stops.back().second;
}

const QFont::Tag kWeightAxis("wght");
} // namespace

Fonts* Fonts::instance()
{
    static Fonts* fonts = [] {
        auto* created = new Fonts(QCoreApplication::instance());
        return created;
    }();
    return fonts;
}

Fonts* Fonts::create(QQmlEngine*, QJSEngine*)
{
    Fonts* fonts = instance();
    QJSEngine::setObjectOwnership(fonts, QJSEngine::CppOwnership);
    return fonts;
}

Fonts::Fonts(QObject* parent)
    : QObject(parent), m_symbols(registerFont(kSymbolsPath)),
      m_system(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family())
{
    if (QFile::exists(kMiSansPath))
        m_bundled = registerFont(kMiSansPath);
    else
        qCInfo(lcFonts) << "no bundled text font in this build; using the system font";
    update();
}

void Fonts::setSource(TextSource source)
{
    if (m_source == source)
        return;
    m_source = source;
    update();
}

void Fonts::setCustomFamily(const QString& family)
{
    if (m_customFamily == family)
        return;
    m_customFamily = family;
    update();
}

void Fonts::update()
{
    QString family = m_bundled.isEmpty() ? m_system : m_bundled;
    if (m_source == System)
        family = m_system;
    else if (m_source == Custom && QFontDatabase::hasFamily(m_customFamily))
        family = m_customFamily;
    if (family == m_sans)
        return;
    m_sans = family;

    // Text that sets no font of its own follows too.
    QFont base = QGuiApplication::font();
    const QFont styled = font(QFont::Normal, base.pixelSize() > 0 ? base.pixelSize() : 14);
    base.setFamilies(styled.families());
    base.clearVariableAxes();
    for (const QFont::Tag& tag : styled.variableAxisTags())
        base.setVariableAxis(tag, styled.variableAxisValue(tag));
    QGuiApplication::setFont(base);
    emit sansChanged();
}

QFont Fonts::font(qreal weight, qreal pixelSize) const
{
    QFont font(m_sans);
    font.setPixelSize(qRound(pixelSize));
    if (!m_bundled.isEmpty() && m_sans == m_bundled) {
        // Normal keeps Qt from faking bold on top of the axis.
        font.setWeight(QFont::Normal);
        font.setVariableAxis(kWeightAxis, float(misansWeight(weight)));
    } else {
        font.setWeight(QFont::Weight(std::clamp(qRound(weight), 1, 1000)));
    }
    return font;
}

QFont Fonts::tabular(QFont font)
{
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

QStringList Fonts::families() const
{
    QStringList families = QFontDatabase::families();
    families.removeIf([](const QString& family) { return QFontDatabase::isPrivateFamily(family); });
    return families;
}

} // namespace cirrus::ui
