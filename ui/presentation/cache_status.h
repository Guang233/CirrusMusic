#pragma once

#include "request.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class CacheSource;

// App.cache: how much the resource cache holds (covers, song details, lyrics) and
// clearing it, for the settings page. Reads the usage on refresh() (the page calls it
// when shown), not continuously.
class CacheStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.cache")
    // -1 until known (or when reading it failed).
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY usageChanged)
    Q_PROPERTY(qint64 limitBytes READ limitBytes NOTIFY usageChanged)
    Q_PROPERTY(bool clearing READ clearing NOTIFY clearingChanged)

  public:
    CacheStatus(CacheSource* source, QObject* parent = nullptr);

    [[nodiscard]] qint64 bytes() const { return m_bytes; }
    [[nodiscard]] qint64 limitBytes() const { return m_limitBytes; }
    [[nodiscard]] bool clearing() const { return m_clearing.pending(); }

    Q_INVOKABLE void refresh();
    // Clears the cache, then reads the usage again; `cleared` tells how it went.
    Q_INVOKABLE void clear();

  signals:
    void usageChanged();
    void clearingChanged();
    void cleared(bool ok);

  private:
    CacheSource* m_source;
    qint64 m_bytes = -1;
    qint64 m_limitBytes = -1;
    Request m_usage;
    Request m_clearing;
};

} // namespace cirrus::ui
