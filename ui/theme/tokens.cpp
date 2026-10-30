#include "tokens.h"

#include "fonts.h"
#include "spring_math.h"

#include <array>
#include <atomic>

namespace cirrus::ui
{
namespace
{

struct TypeToken {
    qreal size;
    qreal lineHeight;
    int weight;
    qreal tracking;
};

TypeStyle makeStyle(TypeToken token)
{
    TypeStyle style;
    style.size = token.size;
    style.lineHeight = token.lineHeight;
    style.weight = token.weight;
    style.tracking = token.tracking;
    style.font = Fonts::instance()->font(token.weight, token.size);
    style.font.setLetterSpacing(QFont::AbsoluteSpacing, token.tracking);
    return style;
}

struct SpringToken {
    qreal stiffness;
    qreal dampingRatio;
    bool spatial;
};

// androidx compose material3 MotionScheme: expressive and standard schemes.
constexpr std::array<SpringToken, Motion::TokenCount> kSprings = {{
    {800, 0.6, true},  // FastSpatial (expressive)
    {380, 0.8, true},  // DefaultSpatial (expressive)
    {200, 0.8, true},  // SlowSpatial (expressive)
    {1400, 0.9, true}, // StandardFastSpatial
    {700, 0.9, true},  // StandardDefaultSpatial
    {300, 0.9, true},  // StandardSlowSpatial
    {3800, 1.0, false},
    {1600, 1.0, false},
    {800, 1.0, false},
}};

const std::array<double, Motion::TokenCount>& settleSeconds()
{
    static const std::array<double, Motion::TokenCount> seconds = [] {
        std::array<double, Motion::TokenCount> result{};
        for (int i = 0; i < Motion::TokenCount; ++i)
            result[i] = springSettleSeconds(kSprings[i].stiffness, kSprings[i].dampingRatio);
        return result;
    }();
    return seconds;
}

// QEasingCurve only accepts plain function pointers, so one instantiation per token.
template <int Token> qreal springEasing(qreal progress)
{
    if (progress >= 1.0)
        return 1.0;
    const SpringToken& t = kSprings[Token];
    const double seconds = progress * settleSeconds()[Token];
    return 1.0 + springAdvance(t.stiffness, t.dampingRatio, {-1.0, 0.0}, seconds).displacement;
}

template <std::size_t... I>
constexpr std::array<QEasingCurve::EasingFunction, sizeof...(I)>
makeEasingTable(std::index_sequence<I...>)
{
    return {&springEasing<static_cast<int>(I)>...};
}

constexpr auto kEasings = makeEasingTable(std::make_index_sequence<Motion::TokenCount>{});

std::atomic<bool> g_reducedMotion{false};

} // namespace

TypeScale::TypeScale(QObject* parent) : QObject(parent)
{
    rebuild();
    connect(Fonts::instance(), &Fonts::sansChanged, this, [this]() {
        rebuild();
        emit changed();
    });
}

void TypeScale::rebuild()
{
    // size, line height, weight, tracking; baseline then emphasized.
    m_displayLarge = makeStyle({57, 64, 400, -0.2});
    m_displayLargeEmphasized = makeStyle({57, 64, 500, 0});
    m_displayMedium = makeStyle({45, 52, 400, 0});
    m_displayMediumEmphasized = makeStyle({45, 52, 500, 0});
    m_displaySmall = makeStyle({36, 44, 400, 0});
    m_displaySmallEmphasized = makeStyle({36, 44, 500, 0});
    m_headlineLarge = makeStyle({32, 40, 400, 0});
    m_headlineLargeEmphasized = makeStyle({32, 40, 500, 0});
    m_headlineMedium = makeStyle({28, 36, 400, 0});
    m_headlineMediumEmphasized = makeStyle({28, 36, 500, 0});
    m_headlineSmall = makeStyle({24, 32, 400, 0});
    m_headlineSmallEmphasized = makeStyle({24, 32, 500, 0});
    m_titleLarge = makeStyle({22, 28, 400, 0});
    m_titleLargeEmphasized = makeStyle({22, 28, 500, 0});
    m_titleMedium = makeStyle({16, 24, 500, 0.2});
    m_titleMediumEmphasized = makeStyle({16, 24, 700, 0.15});
    m_titleSmall = makeStyle({14, 20, 500, 0.1});
    m_titleSmallEmphasized = makeStyle({14, 20, 700, 0.1});
    m_bodyLarge = makeStyle({16, 24, 400, 0.5});
    m_bodyLargeEmphasized = makeStyle({16, 24, 500, 0.15});
    m_bodyMedium = makeStyle({14, 20, 400, 0.2});
    m_bodyMediumEmphasized = makeStyle({14, 20, 500, 0.25});
    m_bodySmall = makeStyle({12, 16, 400, 0.4});
    m_bodySmallEmphasized = makeStyle({12, 16, 500, 0.4});
    m_labelLarge = makeStyle({14, 20, 500, 0.1});
    m_labelLargeEmphasized = makeStyle({14, 20, 700, 0.1});
    m_labelMedium = makeStyle({12, 16, 500, 0.5});
    m_labelMediumEmphasized = makeStyle({12, 16, 700, 0.5});
    m_labelSmall = makeStyle({11, 16, 500, 0.5});
    m_labelSmallEmphasized = makeStyle({11, 16, 700, 0.5});
}

int SpringSpec::duration() const
{
    const double seconds =
        token >= 0 ? settleSeconds()[token] : springSettleSeconds(stiffness, dampingRatio);
    return qRound(seconds * 1000.0);
}

QEasingCurve SpringSpec::easing() const
{
    QEasingCurve curve;
    if (token >= 0)
        curve.setCustomType(kEasings[token]);
    else
        curve.setType(QEasingCurve::OutCubic); // custom springs: use the Spring element
    return curve;
}

Motion::Motion(QObject* parent) : QObject(parent) {}

SpringSpec Motion::spec(Token token)
{
    SpringSpec spec;
    spec.stiffness = kSprings[token].stiffness;
    spec.dampingRatio = kSprings[token].dampingRatio;
    spec.spatial = kSprings[token].spatial;
    spec.token = token;
    return spec;
}

SpringSpec Motion::custom(qreal dampingRatio, qreal stiffness, bool spatial) const
{
    SpringSpec spec;
    spec.dampingRatio = std::max(dampingRatio, 0.01);
    spec.stiffness = std::max(stiffness, 1.0);
    spec.spatial = spatial;
    return spec;
}

qreal Motion::segment(qreal p, qreal from, qreal to) const
{
    if (to <= from)
        return p >= to ? 1.0 : 0.0;
    return std::clamp((p - from) / (to - from), 0.0, 1.0);
}

qreal Motion::lerp(qreal a, qreal b, qreal p) const { return a + (b - a) * p; }

QList<qreal> Motion::curve(const SpringSpec& spec, int samples, qreal seconds) const
{
    QList<qreal> points;
    samples = std::clamp(samples, 2, 4096);
    points.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = seconds * i / (samples - 1);
        points << 1.0 +
                      springAdvance(spec.stiffness, spec.dampingRatio, {-1.0, 0.0}, t).displacement;
    }
    return points;
}

bool Motion::reducedMotion() { return g_reducedMotion.load(std::memory_order_relaxed); }

void Motion::setReduced(bool reduced)
{
    if (g_reducedMotion.exchange(reduced, std::memory_order_relaxed) != reduced)
        emit reducedChanged();
}

} // namespace cirrus::ui
