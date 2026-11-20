#include "cover_scheme.h"

#include "cover_palette.h"
#include "logging/log.h"
#include "theme.h"
#include "tokens.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QThreadPool>

namespace cirrus::ui
{

CoverScheme::CoverScheme(QObject* parent) : QObject(parent), m_scheme(new ColorScheme(this)) {}

CoverScheme::~CoverScheme()
{
    if (m_response)
        m_response->cancel();
}

void CoverScheme::componentComplete()
{
    m_complete = true;
    if (QQmlEngine* engine = qmlEngine(this))
        m_theme = engine->singletonInstance<Theme*>("Cirrus.Theme", "Theme");
    if (m_theme) {
        // Untinted it is the app's scheme; tinted it still follows the theme's variant,
        // spec and contrast.
        connect(m_theme, &Theme::seedChanged, this, [this] {
            if (!tinted())
                update(true);
        });
        for (auto signal :
             {&Theme::variantChanged, &Theme::specVersionChanged, &Theme::contrastChanged})
            connect(m_theme, signal, this, [this] { update(true); });
    }
    update(false);
    request();
}

void CoverScheme::setCover(const QUrl& cover)
{
    if (cover == m_cover)
        return;
    m_cover = cover;
    emit coverChanged();
    if (m_complete)
        request();
}

void CoverScheme::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged();
    update(true);
}

// The cover at the quantization size, from the provider (and cache) the Image items
// use; its palette is extracted off the GUI thread.
void CoverScheme::request()
{
    const quint64 generation = ++m_generation;
    if (m_response) {
        m_response->cancel();
        m_response = nullptr;
    }
    QQmlEngine* engine = qmlEngine(this);
    auto* provider =
        engine && m_cover.scheme() == QLatin1String("image")
            ? dynamic_cast<QQuickAsyncImageProvider*>(engine->imageProvider(m_cover.host()))
            : nullptr;
    if (!provider) {
        if (!m_cover.isEmpty())
            log::warn(lcUiTheme, "cover_scheme.no_provider", {"provider", m_cover.host()});
        setSeed(generation, {});
        return;
    }
    constexpr int kEdge = 128;
    QQuickImageResponse* response = provider->requestImageResponse(
        m_cover.path(QUrl::FullyEncoded).mid(1), QSize(kEdge, kEdge));
    m_response = response;
    connect(response, &QQuickImageResponse::finished, this, [this, response, generation] {
        if (m_response == response)
            m_response = nullptr;
        QImage image;
        if (response->errorString().isEmpty()) {
            if (QQuickTextureFactory* factory = response->textureFactory()) {
                image = factory->image();
                delete factory;
            }
        }
        response->deleteLater();
        if (generation != m_generation)
            return;
        if (image.isNull()) {
            setSeed(generation, {});
            return;
        }
        // The object may be gone by the time the worker is done.
        QThreadPool::globalInstance()->start(
            [self = QPointer<CoverScheme>(this), generation, image] {
                const CoverPalette palette = extractCoverPalette(image);
                QMetaObject::invokeMethod(
                    qGuiApp,
                    [self, generation, palette] {
                        if (self)
                            self->setSeed(generation, palette.valid ? palette.seed : QColor());
                    },
                    Qt::QueuedConnection);
            });
    });
}

void CoverScheme::setSeed(quint64 generation, const QColor& seed)
{
    if (generation != m_generation || seed == m_seed)
        return;
    const bool was = tinted();
    m_seed = seed;
    update(true);
    if (was != tinted())
        emit tintedChanged();
}

void CoverScheme::update(bool animate)
{
    if (!m_theme)
        return;
    const SpringSpec spec = Motion::spec(Motion::SlowEffects);
    m_scheme->transitionTo(m_theme->computeColors(tinted() ? m_seed : m_theme->seed(), m_dark),
                           animate && !Motion::reducedMotion() ? spec.duration() : 0,
                           spec.easing());
}

} // namespace cirrus::ui
