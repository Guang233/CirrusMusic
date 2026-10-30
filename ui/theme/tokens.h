#pragma once

#include <QEasingCurve>
#include <QFont>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// One MD3 type style. Use `font` plus `lineHeight` (Text.FixedHeight).
class TypeStyle
{
    Q_GADGET
    QML_VALUE_TYPE(typeStyle)
    Q_PROPERTY(QFont font MEMBER font CONSTANT)
    Q_PROPERTY(qreal size MEMBER size CONSTANT)
    Q_PROPERTY(qreal lineHeight MEMBER lineHeight CONSTANT)
    Q_PROPERTY(int weight MEMBER weight CONSTANT)
    Q_PROPERTY(qreal tracking MEMBER tracking CONSTANT)

  public:
    QFont font;
    qreal size = 14;
    qreal lineHeight = 20;
    int weight = 400;
    qreal tracking = 0;
};

#define CIRRUS_TYPE_STYLES(X)                                                                      \
    X(displayLarge)                                                                                \
    X(displayMedium)                                                                               \
    X(displaySmall)                                                                                \
    X(headlineLarge)                                                                               \
    X(headlineMedium)                                                                              \
    X(headlineSmall)                                                                               \
    X(titleLarge)                                                                                  \
    X(titleMedium)                                                                                 \
    X(titleSmall)                                                                                  \
    X(bodyLarge)                                                                                   \
    X(bodyMedium)                                                                                  \
    X(bodySmall)                                                                                   \
    X(labelLarge)                                                                                  \
    X(labelMedium)                                                                                 \
    X(labelSmall)

// MD3 type scale: 15 baseline styles and 15 emphasized styles.
// Values from androidx compose material3 TypeScaleTokens (2026-08-05).
class TypeScale : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use Theme.type")
    Q_PROPERTY(cirrus::ui::TypeStyle displayLarge READ displayLarge CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle displayMedium READ displayMedium CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle displaySmall READ displaySmall CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle headlineLarge READ headlineLarge CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle headlineMedium READ headlineMedium CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle headlineSmall READ headlineSmall CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleLarge READ titleLarge CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleMedium READ titleMedium CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleSmall READ titleSmall CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodyLarge READ bodyLarge CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodyMedium READ bodyMedium CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodySmall READ bodySmall CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelLarge READ labelLarge CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelMedium READ labelMedium CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelSmall READ labelSmall CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle displayLargeEmphasized READ displayLargeEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle displayMediumEmphasized READ displayMediumEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle displaySmallEmphasized READ displaySmallEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle headlineLargeEmphasized READ headlineLargeEmphasized CONSTANT)
    Q_PROPERTY(
        cirrus::ui::TypeStyle headlineMediumEmphasized READ headlineMediumEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle headlineSmallEmphasized READ headlineSmallEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleLargeEmphasized READ titleLargeEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleMediumEmphasized READ titleMediumEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle titleSmallEmphasized READ titleSmallEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodyLargeEmphasized READ bodyLargeEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodyMediumEmphasized READ bodyMediumEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle bodySmallEmphasized READ bodySmallEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelLargeEmphasized READ labelLargeEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelMediumEmphasized READ labelMediumEmphasized CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeStyle labelSmallEmphasized READ labelSmallEmphasized CONSTANT)

  public:
    explicit TypeScale(const QString& family, QObject* parent = nullptr);

#define CIRRUS_TYPE_GETTERS(name)                                                                  \
    [[nodiscard]] TypeStyle name() const { return m_##name; }                                      \
    [[nodiscard]] TypeStyle name##Emphasized() const { return m_##name##Emphasized; }
    CIRRUS_TYPE_STYLES(CIRRUS_TYPE_GETTERS)
#undef CIRRUS_TYPE_GETTERS

  private:
#define CIRRUS_TYPE_MEMBERS(name)                                                                  \
    TypeStyle m_##name;                                                                            \
    TypeStyle m_##name##Emphasized;
    CIRRUS_TYPE_STYLES(CIRRUS_TYPE_MEMBERS)
#undef CIRRUS_TYPE_MEMBERS
};

// MD3 corner radius scale (dp == logical px). `full` is clamped by Rectangle.
class ShapeScale
{
    Q_GADGET
    QML_VALUE_TYPE(shapeScale)
    Q_PROPERTY(qreal none MEMBER none CONSTANT)
    Q_PROPERTY(qreal extraSmall MEMBER extraSmall CONSTANT)
    Q_PROPERTY(qreal small MEMBER small CONSTANT)
    Q_PROPERTY(qreal medium MEMBER medium CONSTANT)
    Q_PROPERTY(qreal large MEMBER large CONSTANT)
    Q_PROPERTY(qreal largeIncreased MEMBER largeIncreased CONSTANT)
    Q_PROPERTY(qreal extraLarge MEMBER extraLarge CONSTANT)
    Q_PROPERTY(qreal extraLargeIncreased MEMBER extraLargeIncreased CONSTANT)
    Q_PROPERTY(qreal extraExtraLarge MEMBER extraExtraLarge CONSTANT)
    Q_PROPERTY(qreal full MEMBER full CONSTANT)

  public:
    qreal none = 0;
    qreal extraSmall = 4;
    qreal small = 8;
    qreal medium = 12;
    qreal large = 16;
    qreal largeIncreased = 20;
    qreal extraLarge = 28;
    qreal extraLargeIncreased = 32;
    qreal extraExtraLarge = 48;
    qreal full = 9999;
};

// MD3 state layer and disabled opacities.
class StateLayers
{
    Q_GADGET
    QML_VALUE_TYPE(stateLayers)
    Q_PROPERTY(qreal hover MEMBER hover CONSTANT)
    Q_PROPERTY(qreal focus MEMBER focus CONSTANT)
    Q_PROPERTY(qreal pressed MEMBER pressed CONSTANT)
    Q_PROPERTY(qreal dragged MEMBER dragged CONSTANT)
    Q_PROPERTY(qreal disabledContent MEMBER disabledContent CONSTANT)
    Q_PROPERTY(qreal disabledContainer MEMBER disabledContainer CONSTANT)

  public:
    qreal hover = 0.08;
    qreal focus = 0.10;
    qreal pressed = 0.10;
    qreal dragged = 0.16;
    qreal disabledContent = 0.38;
    qreal disabledContainer = 0.12;
};

// MD3 elevation levels 0-5 in dp.
class ElevationScale
{
    Q_GADGET
    QML_VALUE_TYPE(elevationScale)
    Q_PROPERTY(qreal level0 MEMBER level0 CONSTANT)
    Q_PROPERTY(qreal level1 MEMBER level1 CONSTANT)
    Q_PROPERTY(qreal level2 MEMBER level2 CONSTANT)
    Q_PROPERTY(qreal level3 MEMBER level3 CONSTANT)
    Q_PROPERTY(qreal level4 MEMBER level4 CONSTANT)
    Q_PROPERTY(qreal level5 MEMBER level5 CONSTANT)

  public:
    qreal level0 = 0;
    qreal level1 = 1;
    qreal level2 = 3;
    qreal level3 = 6;
    qreal level4 = 8;
    qreal level5 = 12;
};

// A spring motion token. `duration` and `easing` let any Qt animation follow
// the same curve (for uninterrupted transitions); use the Spring element for
// interactive, retargetable motion that keeps its velocity.
class SpringSpec
{
    Q_GADGET
    QML_VALUE_TYPE(springSpec)
    Q_PROPERTY(qreal stiffness MEMBER stiffness)
    Q_PROPERTY(qreal dampingRatio MEMBER dampingRatio)
    Q_PROPERTY(bool spatial MEMBER spatial)
    Q_PROPERTY(int duration READ duration)
    Q_PROPERTY(QEasingCurve easing READ easing)

  public:
    qreal stiffness = 380;
    qreal dampingRatio = 0.8;
    bool spatial = true;
    int token = -1; // index of a built-in token, -1 for a custom spring

    [[nodiscard]] int duration() const;
    [[nodiscard]] QEasingCurve easing() const;
    friend bool operator==(const SpringSpec&, const SpringSpec&) = default;
};

// MD3 motion scheme. Spatial: position, size, scale (may overshoot).
// Effects: color, opacity (critically damped). Default = Expressive scheme.
class Motion : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use Theme.motion")
    Q_PROPERTY(bool reduced READ reduced WRITE setReduced NOTIFY reducedChanged)
    Q_PROPERTY(cirrus::ui::SpringSpec fastSpatial READ fastSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec defaultSpatial READ defaultSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec slowSpatial READ slowSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec standardFastSpatial READ standardFastSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec standardDefaultSpatial READ standardDefaultSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec standardSlowSpatial READ standardSlowSpatial CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec fastEffects READ fastEffects CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec defaultEffects READ defaultEffects CONSTANT)
    Q_PROPERTY(cirrus::ui::SpringSpec slowEffects READ slowEffects CONSTANT)

  public:
    enum Token {
        FastSpatial,
        DefaultSpatial,
        SlowSpatial,
        StandardFastSpatial,
        StandardDefaultSpatial,
        StandardSlowSpatial,
        FastEffects,
        DefaultEffects,
        SlowEffects,
        TokenCount
    };

    explicit Motion(QObject* parent = nullptr);

    [[nodiscard]] static SpringSpec spec(Token token);
    // Process-wide flag read by Spring and theme transitions.
    [[nodiscard]] static bool reducedMotion();

    [[nodiscard]] bool reduced() const { return reducedMotion(); }
    void setReduced(bool reduced);

    // A spring outside the token set, e.g. the loading indicator's 0.6 / 200 morph.
    Q_INVOKABLE cirrus::ui::SpringSpec custom(qreal dampingRatio, qreal stiffness,
                                              bool spatial = true) const;

    // Progress `p` mapped from [from, to] to 0-1 and clamped: the slice of a shared
    // transition progress one element animates in (docs/ui/motion-choreography.md §1.2).
    // Use for opacity and color; geometry uses lerp() to keep the spring's overshoot.
    Q_INVOKABLE qreal segment(qreal p, qreal from, qreal to) const;
    // `a` at p = 0, `b` at p = 1; not clamped.
    Q_INVOKABLE qreal lerp(qreal a, qreal b, qreal p) const;

    // Position of a 0 -> 1 step at `samples` evenly spaced times in [0, seconds].
    Q_INVOKABLE QList<qreal> curve(const cirrus::ui::SpringSpec& spec, int samples,
                                   qreal seconds) const;

    [[nodiscard]] SpringSpec fastSpatial() const { return spec(FastSpatial); }
    [[nodiscard]] SpringSpec defaultSpatial() const { return spec(DefaultSpatial); }
    [[nodiscard]] SpringSpec slowSpatial() const { return spec(SlowSpatial); }
    [[nodiscard]] SpringSpec standardFastSpatial() const { return spec(StandardFastSpatial); }
    [[nodiscard]] SpringSpec standardDefaultSpatial() const { return spec(StandardDefaultSpatial); }
    [[nodiscard]] SpringSpec standardSlowSpatial() const { return spec(StandardSlowSpatial); }
    [[nodiscard]] SpringSpec fastEffects() const { return spec(FastEffects); }
    [[nodiscard]] SpringSpec defaultEffects() const { return spec(DefaultEffects); }
    [[nodiscard]] SpringSpec slowEffects() const { return spec(SlowEffects); }

  signals:
    void reducedChanged();
};

} // namespace cirrus::ui
