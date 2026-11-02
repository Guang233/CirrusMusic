#include "app_settings.h"

namespace cirrus::ui
{

namespace
{
constexpr int kSeedSourceSystem = 2; // Theme.SeedSource.System
} // namespace

AppSettings::AppSettings(QObject* parent) : QObject(parent) {}

template <class T> T AppSettings::read(const char* key, T fallback) const
{
    return m_store.value(QLatin1StringView(key), QVariant::fromValue(fallback)).template value<T>();
}

// Returns whether the stored value changed.
template <class T> bool AppSettings::write(const char* key, const T& value, T fallback)
{
    if (read(key, fallback) == value)
        return false;
    m_store.setValue(QLatin1StringView(key), QVariant::fromValue(value));
    return true;
}

int AppSettings::themeMode() const { return read("appearance/themeMode", 0); }

void AppSettings::setThemeMode(int mode)
{
    if (write("appearance/themeMode", mode, 0))
        emit themeModeChanged();
}

int AppSettings::seedSource() const { return read("appearance/seedSource", kSeedSourceSystem); }

void AppSettings::setSeedSource(int source)
{
    if (write("appearance/seedSource", source, kSeedSourceSystem))
        emit seedSourceChanged();
}

int AppSettings::variant() const { return read("appearance/variant", 0); }

void AppSettings::setVariant(int variant)
{
    if (write("appearance/variant", variant, 0))
        emit variantChanged();
}

bool AppSettings::reducedMotion() const { return read("appearance/reducedMotion", false); }

void AppSettings::setReducedMotion(bool reduced)
{
    if (write("appearance/reducedMotion", reduced, false))
        emit reducedMotionChanged();
}

int AppSettings::fontSource() const { return read("appearance/fontSource", 0); }

void AppSettings::setFontSource(int source)
{
    if (write("appearance/fontSource", source, 0))
        emit fontSourceChanged();
}

QString AppSettings::customFontFamily() const
{
    return read("appearance/customFontFamily", QString());
}

void AppSettings::setCustomFontFamily(const QString& family)
{
    if (write("appearance/customFontFamily", family, QString()))
        emit customFontFamilyChanged();
}

int AppSettings::titleBar() const { return read("window/titleBar", -1); }

void AppSettings::setTitleBar(int mode)
{
    if (write("window/titleBar", mode, -1))
        emit titleBarChanged();
}

int AppSettings::closeAction() const { return read("window/closeAction", -1); }

void AppSettings::setCloseAction(int action)
{
    if (write("window/closeAction", action, -1))
        emit closeActionChanged();
}

bool AppSettings::preferDiscreteGpu() const { return read("graphics/preferDiscreteGpu", true); }

void AppSettings::setPreferDiscreteGpu(bool prefer)
{
    if (write("graphics/preferDiscreteGpu", prefer, true))
        emit preferDiscreteGpuChanged();
}

QStringList AppSettings::searchHistory() const { return read("search/history", QStringList()); }

void AppSettings::setSearchHistory(const QStringList& history)
{
    if (write("search/history", history, QStringList()))
        emit searchHistoryChanged();
}

bool AppSettings::showTranslation() const { return read("lyrics/showTranslation", true); }

void AppSettings::setShowTranslation(bool show)
{
    if (write("lyrics/showTranslation", show, true))
        emit showTranslationChanged();
}

bool AppSettings::showRomanization() const { return read("lyrics/showRomanization", false); }

void AppSettings::setShowRomanization(bool show)
{
    if (write("lyrics/showRomanization", show, false))
        emit showRomanizationChanged();
}

} // namespace cirrus::ui
