#pragma once

#include "color_scheme.h"

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QQuickImageResponse;

namespace cirrus::ui
{

class Theme;

// The color roles of one cover, for a part of a page that takes that cover's colors
// (the info pane of a playlist): the theme's scheme generation with the cover's seed.
// Until the cover's colors are known, and for a cover without any, it holds the app's
// own scheme, so the part looks like the rest of the app meanwhile.
//   CoverScheme { id: tint; cover: "image://cover/" + key; dark: Theme.dark }
//   Rectangle { ColorScope.scheme: tint.scheme; color: ColorScope.scheme.primaryContainer }
class CoverScheme : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    // image://<async provider>/<id>; empty for the app's scheme.
    Q_PROPERTY(QUrl cover READ cover WRITE setCover NOTIFY coverChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(cirrus::ui::ColorScheme* scheme READ scheme CONSTANT)
    // The scheme is the cover's (not the app's).
    Q_PROPERTY(bool tinted READ tinted NOTIFY tintedChanged)

  public:
    explicit CoverScheme(QObject* parent = nullptr);
    ~CoverScheme() override;

    [[nodiscard]] QUrl cover() const { return m_cover; }
    void setCover(const QUrl& cover);
    [[nodiscard]] bool dark() const { return m_dark; }
    void setDark(bool dark);
    [[nodiscard]] ColorScheme* scheme() const { return m_scheme; }
    [[nodiscard]] bool tinted() const { return m_seed.isValid(); }

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void coverChanged();
    void darkChanged();
    void tintedChanged();

  private:
    void request();
    void setSeed(quint64 generation, const QColor& seed);
    void update(bool animate);

    ColorScheme* m_scheme;
    Theme* m_theme = nullptr;
    QUrl m_cover;
    QColor m_seed; // invalid: the app's seed
    bool m_dark = false;
    bool m_complete = false;
    quint64 m_generation = 0;
    QPointer<QQuickImageResponse> m_response;
};

} // namespace cirrus::ui
