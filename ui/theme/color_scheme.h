#pragma once

#include <QColor>
#include <QObject>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace cirrus::ui
{

// Every MD3 color role: X(property, MCU getter suffix).
#define CIRRUS_COLOR_ROLES(X)                                                                      \
    X(primary, Primary)                                                                            \
    X(onPrimary, OnPrimary)                                                                        \
    X(primaryContainer, PrimaryContainer)                                                          \
    X(onPrimaryContainer, OnPrimaryContainer)                                                      \
    X(inversePrimary, InversePrimary)                                                              \
    X(secondary, Secondary)                                                                        \
    X(onSecondary, OnSecondary)                                                                    \
    X(secondaryContainer, SecondaryContainer)                                                      \
    X(onSecondaryContainer, OnSecondaryContainer)                                                  \
    X(tertiary, Tertiary)                                                                          \
    X(onTertiary, OnTertiary)                                                                      \
    X(tertiaryContainer, TertiaryContainer)                                                        \
    X(onTertiaryContainer, OnTertiaryContainer)                                                    \
    X(error, Error)                                                                                \
    X(onError, OnError)                                                                            \
    X(errorContainer, ErrorContainer)                                                              \
    X(onErrorContainer, OnErrorContainer)                                                          \
    X(primaryFixed, PrimaryFixed)                                                                  \
    X(primaryFixedDim, PrimaryFixedDim)                                                            \
    X(onPrimaryFixed, OnPrimaryFixed)                                                              \
    X(onPrimaryFixedVariant, OnPrimaryFixedVariant)                                                \
    X(secondaryFixed, SecondaryFixed)                                                              \
    X(secondaryFixedDim, SecondaryFixedDim)                                                        \
    X(onSecondaryFixed, OnSecondaryFixed)                                                          \
    X(onSecondaryFixedVariant, OnSecondaryFixedVariant)                                            \
    X(tertiaryFixed, TertiaryFixed)                                                                \
    X(tertiaryFixedDim, TertiaryFixedDim)                                                          \
    X(onTertiaryFixed, OnTertiaryFixed)                                                            \
    X(onTertiaryFixedVariant, OnTertiaryFixedVariant)                                              \
    X(background, Background)                                                                      \
    X(onBackground, OnBackground)                                                                  \
    X(surface, Surface)                                                                            \
    X(surfaceDim, SurfaceDim)                                                                      \
    X(surfaceBright, SurfaceBright)                                                                \
    X(surfaceContainerLowest, SurfaceContainerLowest)                                              \
    X(surfaceContainerLow, SurfaceContainerLow)                                                    \
    X(surfaceContainer, SurfaceContainer)                                                          \
    X(surfaceContainerHigh, SurfaceContainerHigh)                                                  \
    X(surfaceContainerHighest, SurfaceContainerHighest)                                            \
    X(onSurface, OnSurface)                                                                        \
    X(surfaceVariant, SurfaceVariant)                                                              \
    X(onSurfaceVariant, OnSurfaceVariant)                                                          \
    X(inverseSurface, InverseSurface)                                                              \
    X(inverseOnSurface, InverseOnSurface)                                                          \
    X(outline, Outline)                                                                            \
    X(outlineVariant, OutlineVariant)                                                              \
    X(shadow, Shadow)                                                                              \
    X(scrim, Scrim)                                                                                \
    X(surfaceTint, SurfaceTint)

// Live MD3 color roles (Theme.color). Read-only for QML; when the theme changes,
// all roles blend to the new scheme together in CAM16-UCS space.
class ColorScheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use Theme.color")
    Q_PROPERTY(QColor primary READ primary NOTIFY changed)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed)
    Q_PROPERTY(QColor primaryContainer READ primaryContainer NOTIFY changed)
    Q_PROPERTY(QColor onPrimaryContainer READ onPrimaryContainer NOTIFY changed)
    Q_PROPERTY(QColor inversePrimary READ inversePrimary NOTIFY changed)
    Q_PROPERTY(QColor secondary READ secondary NOTIFY changed)
    Q_PROPERTY(QColor onSecondary READ onSecondary NOTIFY changed)
    Q_PROPERTY(QColor secondaryContainer READ secondaryContainer NOTIFY changed)
    Q_PROPERTY(QColor onSecondaryContainer READ onSecondaryContainer NOTIFY changed)
    Q_PROPERTY(QColor tertiary READ tertiary NOTIFY changed)
    Q_PROPERTY(QColor onTertiary READ onTertiary NOTIFY changed)
    Q_PROPERTY(QColor tertiaryContainer READ tertiaryContainer NOTIFY changed)
    Q_PROPERTY(QColor onTertiaryContainer READ onTertiaryContainer NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(QColor onError READ onError NOTIFY changed)
    Q_PROPERTY(QColor errorContainer READ errorContainer NOTIFY changed)
    Q_PROPERTY(QColor onErrorContainer READ onErrorContainer NOTIFY changed)
    Q_PROPERTY(QColor primaryFixed READ primaryFixed NOTIFY changed)
    Q_PROPERTY(QColor primaryFixedDim READ primaryFixedDim NOTIFY changed)
    Q_PROPERTY(QColor onPrimaryFixed READ onPrimaryFixed NOTIFY changed)
    Q_PROPERTY(QColor onPrimaryFixedVariant READ onPrimaryFixedVariant NOTIFY changed)
    Q_PROPERTY(QColor secondaryFixed READ secondaryFixed NOTIFY changed)
    Q_PROPERTY(QColor secondaryFixedDim READ secondaryFixedDim NOTIFY changed)
    Q_PROPERTY(QColor onSecondaryFixed READ onSecondaryFixed NOTIFY changed)
    Q_PROPERTY(QColor onSecondaryFixedVariant READ onSecondaryFixedVariant NOTIFY changed)
    Q_PROPERTY(QColor tertiaryFixed READ tertiaryFixed NOTIFY changed)
    Q_PROPERTY(QColor tertiaryFixedDim READ tertiaryFixedDim NOTIFY changed)
    Q_PROPERTY(QColor onTertiaryFixed READ onTertiaryFixed NOTIFY changed)
    Q_PROPERTY(QColor onTertiaryFixedVariant READ onTertiaryFixedVariant NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor onBackground READ onBackground NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceDim READ surfaceDim NOTIFY changed)
    Q_PROPERTY(QColor surfaceBright READ surfaceBright NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainerLowest READ surfaceContainerLowest NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainerLow READ surfaceContainerLow NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainer READ surfaceContainer NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainerHigh READ surfaceContainerHigh NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainerHighest READ surfaceContainerHighest NOTIFY changed)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY changed)
    Q_PROPERTY(QColor surfaceVariant READ surfaceVariant NOTIFY changed)
    Q_PROPERTY(QColor onSurfaceVariant READ onSurfaceVariant NOTIFY changed)
    Q_PROPERTY(QColor inverseSurface READ inverseSurface NOTIFY changed)
    Q_PROPERTY(QColor inverseOnSurface READ inverseOnSurface NOTIFY changed)
    Q_PROPERTY(QColor outline READ outline NOTIFY changed)
    Q_PROPERTY(QColor outlineVariant READ outlineVariant NOTIFY changed)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed)
    Q_PROPERTY(QColor surfaceTint READ surfaceTint NOTIFY changed)
    Q_PROPERTY(QStringList roles READ roles CONSTANT)

  public:
    enum Role {
#define CIRRUS_ROLE_ENUM(name, Name) Name,
        CIRRUS_COLOR_ROLES(CIRRUS_ROLE_ENUM)
#undef CIRRUS_ROLE_ENUM
            RoleCount
    };
    Q_ENUM(Role)
    using Colors = std::array<QColor, RoleCount>;

    explicit ColorScheme(QObject* parent = nullptr);

#define CIRRUS_ROLE_GETTER(name, Name)                                                             \
    [[nodiscard]] QColor name() const { return m_current[Name]; }
    CIRRUS_COLOR_ROLES(CIRRUS_ROLE_GETTER)
#undef CIRRUS_ROLE_GETTER

    // Property names in declaration order (for galleries and tooling).
    [[nodiscard]] static QStringList roles();
    [[nodiscard]] const Colors& current() const { return m_current; }

    // Blend to `colors` over `durationMs` using `easing`; 0 applies immediately.
    void transitionTo(const Colors& colors, int durationMs, const QEasingCurve& easing);

  signals:
    void changed();

  private:
    void blend(qreal progress);

    Colors m_from{};
    Colors m_to{};
    Colors m_current{};
    QVariantAnimation m_animation;
};

} // namespace cirrus::ui
