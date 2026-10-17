#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Registers the bundled fonts on first use and exposes their family names.
// Latin text uses Google Sans Flex; CJK and other scripts fall back to the
// platform families listed in cjkFallbacks (no CJK font is bundled yet).
class Fonts final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString sans READ sans CONSTANT)
    Q_PROPERTY(QString symbols READ symbols CONSTANT)
    Q_PROPERTY(QStringList cjkFallbacks READ cjkFallbacks CONSTANT)
    Q_PROPERTY(QStringList installedFallbacks READ installedFallbacks CONSTANT)

  public:
    explicit Fonts(QObject* parent = nullptr);

    [[nodiscard]] QString sans() const;
    [[nodiscard]] QString symbols() const;
    [[nodiscard]] QStringList cjkFallbacks() const;
    [[nodiscard]] QStringList installedFallbacks() const;
};

} // namespace cirrus::ui
