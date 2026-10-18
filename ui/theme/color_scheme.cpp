#include "color_scheme.h"

#include "cpp/blend/blend.h"

namespace cirrus::ui
{

ColorScheme::ColorScheme(QObject* parent) : QObject(parent)
{
    m_current.fill(QColor(Qt::black));
    m_animation.setStartValue(0.0);
    m_animation.setEndValue(1.0);
    connect(&m_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { blend(value.toReal()); });
}

QStringList ColorScheme::roles()
{
    return {
#define CIRRUS_ROLE_NAME(name, Name) QStringLiteral(#name),
        CIRRUS_COLOR_ROLES(CIRRUS_ROLE_NAME)
#undef CIRRUS_ROLE_NAME
    };
}

void ColorScheme::transitionTo(const Colors& colors, int durationMs, const QEasingCurve& easing)
{
    m_animation.stop();
    m_to = colors;
    if (durationMs <= 0) {
        m_current = colors;
        emit changed();
        return;
    }
    m_from = m_current; // an interrupted transition continues from what is on screen
    m_animation.setDuration(durationMs);
    m_animation.setEasingCurve(easing);
    m_animation.start();
}

void ColorScheme::blend(qreal progress)
{
    namespace mcu = material_color_utilities;
    const double amount = std::clamp(progress, 0.0, 1.0);
    for (int i = 0; i < RoleCount; ++i) {
        const QRgb from = m_from[i].rgba();
        const QRgb to = m_to[i].rgba();
        if (from == to) {
            m_current[i] = m_to[i];
            continue;
        }
        const mcu::Argb mixed = mcu::BlendCam16Ucs(from | 0xff000000u, to | 0xff000000u, amount);
        QColor color = QColor::fromRgb(mixed);
        color.setAlphaF(m_from[i].alphaF() + (m_to[i].alphaF() - m_from[i].alphaF()) * amount);
        m_current[i] = color;
    }
    emit changed();
}

} // namespace cirrus::ui
