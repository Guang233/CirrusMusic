#include "shadow_owns_material.h"
#include <QQuickItem>

namespace cirrus::ui
{

ShadowOwnsMaterial::ShadowOwnsMaterial(QObject* parent) : QObject(parent) {}

void ShadowOwnsMaterial::componentComplete()
{
    // `material` is the default one unless a custom one is set (that one has its parent).
    auto* shadow = qobject_cast<QQuickItem*>(parent());
    if (!shadow)
        return;
    auto* material = shadow->property("material").value<QQuickItem*>();
    if (material && !material->parent())
        material->setParent(shadow);
}

} // namespace cirrus::ui
