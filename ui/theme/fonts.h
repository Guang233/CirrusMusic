#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Font families for QML. Text uses the platform UI font (no text font is
// bundled yet, see UQ-03); icons use the bundled Material Symbols subset,
// registered on first use.
class Fonts final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString sans READ sans CONSTANT)
    Q_PROPERTY(QString symbols READ symbols CONSTANT)

  public:
    explicit Fonts(QObject* parent = nullptr);

    [[nodiscard]] QString sans() const;
    [[nodiscard]] QString symbols() const;
};

} // namespace cirrus::ui
