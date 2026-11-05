#include "color_scope.h"

#include "theme.h"

#include <QQmlEngine>

namespace cirrus::ui
{

ColorScope::ColorScope(QObject* parent) : QQuickAttachedPropertyPropagator(parent) { initialize(); }

ColorScope* ColorScope::qmlAttachedProperties(QObject* object) { return new ColorScope(object); }

ColorScheme* ColorScope::scheme() const
{
    if (m_scheme)
        return m_scheme;
    if (!m_theme) {
        if (QQmlEngine* engine = qmlEngine(parent())) {
            if (auto* theme = engine->singletonInstance<Theme*>("Cirrus.Theme", "Theme"))
                m_theme = theme->color();
        }
    }
    return m_theme;
}

void ColorScope::setScheme(ColorScheme* scheme)
{
    m_explicit = true;
    if (m_scheme == scheme)
        return;
    m_scheme = scheme;
    propagate();
    emit schemeChanged();
}

void ColorScope::resetScheme()
{
    if (!m_explicit)
        return;
    m_explicit = false;
    auto* parentScope = qobject_cast<ColorScope*>(attachedParent());
    inherit(parentScope ? parentScope->m_scheme.data() : nullptr);
}

void ColorScope::attachedParentChange(QQuickAttachedPropertyPropagator* newParent,
                                      QQuickAttachedPropertyPropagator*)
{
    if (auto* parentScope = qobject_cast<ColorScope*>(newParent))
        inherit(parentScope->m_scheme);
    else
        inherit(nullptr);
}

void ColorScope::inherit(ColorScheme* scheme)
{
    if (m_explicit || m_scheme == scheme)
        return;
    m_scheme = scheme;
    propagate();
    emit schemeChanged();
}

void ColorScope::propagate()
{
    const auto children = attachedChildren();
    for (QQuickAttachedPropertyPropagator* child : children) {
        if (auto* scope = qobject_cast<ColorScope*>(child))
            scope->inherit(m_scheme);
    }
}

} // namespace cirrus::ui
