#pragma once

#include <QObject>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Hands the RectangularShadow it sits in its default material to own, so that the material
// goes with it (docs/ui/controls.md §2). Qt (6.11, 6.12, dev) makes that ShaderEffect in
// QQuickRectangularShadowPrivate's constructor, before the shadow is known there
// (`new QQuickShaderEffect(q)` while q is still null), and never deletes it: every shadow left
// a ShaderEffect, its mesh and its shader manager behind. Shadow.qml carries one.
//   RectangularShadow { ShadowOwnsMaterial {} }
class ShadowOwnsMaterial : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

  public:
    explicit ShadowOwnsMaterial(QObject* parent = nullptr);

    void classBegin() override {}
    void componentComplete() override;
};

} // namespace cirrus::ui
