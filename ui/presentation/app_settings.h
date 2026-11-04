#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// Declares one setting of AppSettings: a property `name` (READ name, WRITE setter,
// NOTIFY nameChanged) stored under `key`, `fallback` until the user changes it. The
// setter stores and notifies only on a change. Used once per setting in AppSettings;
// moc expands it like the Q_PROPERTY it contains.
#define CIRRUS_SETTING(Type, name, setter, key, fallback)                                          \
    Q_PROPERTY(Type name READ name WRITE setter NOTIFY name##Changed)                              \
                                                                                                   \
  public:                                                                                          \
    [[nodiscard]] Type name() const { return read<Type>(key, fallback); }                          \
    void setter(const Type& value)                                                                 \
    {                                                                                              \
        if (write<Type>(key, value, fallback))                                                     \
            emit name##Changed();                                                                  \
    }                                                                                              \
    Q_SIGNAL void name##Changed();                                                                 \
                                                                                                   \
  private:

namespace cirrus::ui
{

// User settings (App.settings), persisted with QSettings. Appearance values are the
// integer values of the matching Theme and Fonts enums, which Main.qml binds to them;
// this layer does not depend on the theme module.
//
// Adding a setting is one CIRRUS_SETTING line here (with what its values mean); the
// settings page shows it with a row (docs/ui/pages/settings.md §7). Keys are stored
// on disk: keep them when renaming a property.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.settings")

    // Appearance.
    // Theme.Mode: System (0), Light, Dark.
    CIRRUS_SETTING(int, themeMode, setThemeMode, "appearance/themeMode", 0)
    // Theme.SeedSource: Cover (0), Fixed, System (2, default).
    CIRRUS_SETTING(int, seedSource, setSeedSource, "appearance/seedSource", 2)
    // The seed for SeedSource Fixed (MD3's baseline purple until one is picked).
    CIRRUS_SETTING(QColor, fixedSeed, setFixedSeed, "appearance/fixedSeed", QColor(0x6750a4))
    // Theme.Variant: TonalSpot (0) …
    CIRRUS_SETTING(int, variant, setVariant, "appearance/variant", 0)
    CIRRUS_SETTING(bool, reducedMotion, setReducedMotion, "appearance/reducedMotion", false)
    // Fonts.TextSource: Bundled (0, MiSans, default), System, Custom.
    CIRRUS_SETTING(int, fontSource, setFontSource, "appearance/fontSource", 0)
    // Family for fontSource Custom.
    CIRRUS_SETTING(QString, customFontFamily, setCustomFontFamily, "appearance/customFontFamily",
                   QString())
    // Font files picked for the custom font (not installed in the system), loaded at
    // startup.
    CIRRUS_SETTING(QStringList, customFontFiles, setCustomFontFiles, "appearance/customFontFiles",
                   QStringList())

    // Window and system.
    // TitleBarMode (Cirrus.Window), or -1 until the user picks one: the default then
    // follows the desktop. Applied at startup; see docs/ui/README.md §6.1.
    CIRRUS_SETTING(int, titleBar, setTitleBar, "window/titleBar", -1)
    // What closing the main window does when a tray icon is shown: -1 ask (default),
    // 0 quit, 1 hide to the tray and keep playing. Without a tray it always quits.
    CIRRUS_SETTING(int, closeAction, setCloseAction, "window/closeAction", -1)
    // Render on the discrete GPU of a hybrid-graphics Linux machine. Applied at
    // startup, before the window exists (ui/app/gpu_preference.h).
    CIRRUS_SETTING(bool, preferDiscreteGpu, setPreferDiscreteGpu, "graphics/preferDiscreteGpu",
                   true)

    // Playback.
    // Requested stream quality, the backend's AudioQuality: Standard (0), Exhigh (1,
    // default), Lossless, HiRes. The service answers with the best it has up to that.
    CIRRUS_SETTING(int, audioQuality, setAudioQuality, "playback/audioQuality", 1)

    // Lyrics.
    CIRRUS_SETTING(bool, showTranslation, setShowTranslation, "lyrics/showTranslation", true)
    CIRRUS_SETTING(bool, showRomanization, setShowRomanization, "lyrics/showRomanization", false)

    // Recent searches, newest first (docs/ui/pages/search.md §2); only on this machine.
    CIRRUS_SETTING(QStringList, searchHistory, setSearchHistory, "search/history", QStringList())

  public:
    explicit AppSettings(QObject* parent = nullptr);

  private:
    template <class T> T read(const char* key, const T& fallback) const
    {
        return m_store.value(QLatin1StringView(key), QVariant::fromValue(fallback))
            .template value<T>();
    }
    // Returns whether the stored value changed.
    template <class T> bool write(const char* key, const T& value, const T& fallback)
    {
        if (read(key, fallback) == value)
            return false;
        m_store.setValue(QLatin1StringView(key), QVariant::fromValue(value));
        return true;
    }

    QSettings m_store;
};

} // namespace cirrus::ui
