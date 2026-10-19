#pragma once

#include <QColor>
#include <QObject>

namespace cirrus::ui
{

// Platform accent color and light/dark preference.
// Linux: xdg-desktop-portal org.freedesktop.appearance (accent-color,
// color-scheme), read over D-Bus and followed via SettingChanged, because Qt
// does not expose the portal accent. Elsewhere: QPalette::Accent and
// QStyleHints::colorScheme.
class SystemAppearance : public QObject
{
    Q_OBJECT

  public:
    explicit SystemAppearance(QObject* parent = nullptr);

    // Invalid when the platform provides no accent color.
    [[nodiscard]] QColor accent() const { return m_accent; }
    [[nodiscard]] Qt::ColorScheme colorScheme() const;

    // Portal values (Linux); also used by the private D-Bus watcher.
    void applyPortalValue(const QString& key, const QVariant& value);

  signals:
    void accentChanged();
    void colorSchemeChanged();

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void setAccent(const QColor& accent);

    QColor m_accent;
    Qt::ColorScheme m_portalScheme = Qt::ColorScheme::Unknown;
    bool m_portal = false;
};

} // namespace cirrus::ui
