#pragma once

#include <QObject>
#include <QSettings>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// User settings (App.settings), persisted with QSettings. Appearance values are the
// integer values of the matching Theme enums, which Main.qml binds to Theme; this
// layer does not depend on the theme module.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.settings")
    // Theme.Mode: System (0), Light, Dark.
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    // Theme.SeedSource: Cover (0), Fixed, System (default).
    Q_PROPERTY(int seedSource READ seedSource WRITE setSeedSource NOTIFY seedSourceChanged)
    // Theme.Variant: TonalSpot (0) …
    Q_PROPERTY(int variant READ variant WRITE setVariant NOTIFY variantChanged)
    Q_PROPERTY(
        bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    // TitleBarMode (Cirrus.Window), or -1 until the user picks one: the default then
    // follows the desktop. Applied at startup; see docs/ui/README.md §6.1.
    Q_PROPERTY(int titleBar READ titleBar WRITE setTitleBar NOTIFY titleBarChanged)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation NOTIFY
                   showTranslationChanged)
    Q_PROPERTY(bool showRomanization READ showRomanization WRITE setShowRomanization NOTIFY
                   showRomanizationChanged)

  public:
    explicit AppSettings(QObject* parent = nullptr);

    [[nodiscard]] int themeMode() const;
    void setThemeMode(int mode);
    [[nodiscard]] int seedSource() const;
    void setSeedSource(int source);
    [[nodiscard]] int variant() const;
    void setVariant(int variant);
    [[nodiscard]] bool reducedMotion() const;
    void setReducedMotion(bool reduced);
    [[nodiscard]] int titleBar() const;
    void setTitleBar(int mode);
    [[nodiscard]] bool showTranslation() const;
    void setShowTranslation(bool show);
    [[nodiscard]] bool showRomanization() const;
    void setShowRomanization(bool show);

  signals:
    void themeModeChanged();
    void seedSourceChanged();
    void variantChanged();
    void reducedMotionChanged();
    void titleBarChanged();
    void showTranslationChanged();
    void showRomanizationChanged();

  private:
    template <class T> T read(const char* key, T fallback) const;
    template <class T> bool write(const char* key, const T& value, T fallback);

    QSettings m_store;
};

} // namespace cirrus::ui
