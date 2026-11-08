#pragma once

#include "request.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class AppSettings;
class CacheSource;

// App.cache: how much the resource cache (covers, song details, lyrics) and the song
// cache (songs heard) hold, and clearing them, for the settings page. Reads the usage
// on refresh() (the page calls it when shown), not continuously. The song cache's limit
// follows App.settings.audioCacheLimit.
class CacheStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.cache")
    // -1 until known (or when reading it failed).
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY usageChanged)
    Q_PROPERTY(qint64 limitBytes READ limitBytes NOTIFY usageChanged)
    Q_PROPERTY(bool clearing READ clearing NOTIFY clearingChanged)
    // The song cache; -1 until known.
    Q_PROPERTY(qint64 audioBytes READ audioBytes NOTIFY audioUsageChanged)
    Q_PROPERTY(bool audioClearing READ audioClearing NOTIFY audioClearingChanged)

  public:
    CacheStatus(CacheSource* source, AppSettings* settings = nullptr, QObject* parent = nullptr);

    [[nodiscard]] qint64 bytes() const { return m_bytes; }
    [[nodiscard]] qint64 limitBytes() const { return m_limitBytes; }
    [[nodiscard]] bool clearing() const { return m_clearing.pending(); }
    [[nodiscard]] qint64 audioBytes() const { return m_audioBytes; }
    [[nodiscard]] bool audioClearing() const { return m_audioClearing.pending(); }

    Q_INVOKABLE void refresh();
    // Clears the cache, then reads the usage again; `cleared` tells how it went.
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearAudio();

  signals:
    void usageChanged();
    void clearingChanged();
    void cleared(bool ok);
    void audioUsageChanged();
    void audioClearingChanged();
    void audioCleared(bool ok);

  private:
    CacheSource* m_source;
    qint64 m_bytes = -1;
    qint64 m_limitBytes = -1;
    Request m_usage;
    Request m_clearing;
    qint64 m_audioBytes = -1;
    Request m_audioUsage;
    Request m_audioClearing;
};

} // namespace cirrus::ui
