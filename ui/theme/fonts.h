#pragma once

#include <QFont>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace cirrus::ui
{

// Fonts for the whole UI (docs/ui/README.md §4.2, UQ-03). Text uses the bundled MiSans
// by default; the user may pick the system UI font or any installed family instead
// (`source`, `customFamily`, set from App.settings). Missing glyphs (Korean, emoji…)
// fall back to the system fonts. Icons always use the bundled Material Symbols.
//
// MiSans is one variable font whose `wght` axis runs continuously from 150 to 700, on
// its own scale (Regular is 330, Bold 630), and Qt does not map QFont weights onto it.
// So text fonts come from font(): for MiSans it maps CSS weights onto the axis (any
// value, so weights can animate); other families get plain weights.
class Fonts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(TextSource source READ source WRITE setSource NOTIFY sansChanged)
    // For source Custom; an empty or missing family falls back to the bundled font.
    Q_PROPERTY(QString customFamily READ customFamily WRITE setCustomFamily NOTIFY sansChanged)
    // The text family in use.
    Q_PROPERTY(QString sans READ sans NOTIFY sansChanged)
    Q_PROPERTY(QString symbols READ symbols CONSTANT)
    // False when the build has no bundled text font (prepare_fonts.py not run).
    Q_PROPERTY(bool bundledAvailable READ bundledAvailable CONSTANT)
    Q_PROPERTY(QString systemFamily READ systemFamily CONSTANT)

  public:
    enum TextSource { Bundled, System, Custom };
    Q_ENUM(TextSource)

    // The one instance, created on first use (needs QGuiApplication).
    [[nodiscard]] static Fonts* instance();
    // QML singleton factory: the instance, owned by C++.
    static Fonts* create(QQmlEngine* qml, QJSEngine* js);

    [[nodiscard]] TextSource source() const { return m_source; }
    void setSource(TextSource source);
    [[nodiscard]] QString customFamily() const { return m_customFamily; }
    void setCustomFamily(const QString& family);
    [[nodiscard]] QString sans() const { return m_sans; }
    [[nodiscard]] QString symbols() const { return m_symbols; }
    [[nodiscard]] bool bundledAvailable() const { return !m_bundled.isEmpty(); }
    [[nodiscard]] QString systemFamily() const { return m_system; }

    // Text font at a CSS weight (100–900, fractional allowed) and pixel size, in the
    // family in use.
    Q_INVOKABLE QFont font(qreal weight, qreal pixelSize) const;
    // `font` with tabular (fixed-width) digits, for columns of numbers: MiSans's digits
    // are proportional by default.
    Q_INVOKABLE static QFont tabular(QFont font);
    // Installed families, for picking a custom font.
    Q_INVOKABLE QStringList families() const;

  signals:
    void sansChanged();

  private:
    explicit Fonts(QObject* parent = nullptr);
    void update();

    TextSource m_source = Bundled;
    QString m_customFamily;
    QString m_sans;
    QString m_symbols;
    QString m_bundled;
    QString m_system;
};

} // namespace cirrus::ui
