#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class AppSettings;

// App.diagnostics: the log for the settings page (docs/tasks/logging.md §7). Detailed
// logging stays on for 24 hours, across restarts (App.settings.verboseLogUntil), then
// turns itself off. The files are read on refresh() (the page calls it when shown).
// `available` is false when the log only goes to stderr (the gallery, or no directory).
class Diagnostics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.diagnostics")
    Q_PROPERTY(bool available READ available NOTIFY filesChanged)
    Q_PROPERTY(bool verbose READ verbose WRITE setVerbose NOTIFY verboseChanged)
    // When detailed logging turns off: "14:03", "明天 14:03"; empty when off.
    Q_PROPERTY(QString verboseEnds READ verboseEnds NOTIFY verboseChanged)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY filesChanged)
    // Newest first: {name, bytes, modified (ms since epoch), current}.
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    // Bytes in files of earlier sessions (what clear() removes).
    Q_PROPERTY(qint64 oldBytes READ oldBytes NOTIFY filesChanged)

  public:
    static constexpr qint64 VerboseMs = 24LL * 60 * 60 * 1000;

    explicit Diagnostics(AppSettings* settings, QObject* parent = nullptr);

    [[nodiscard]] bool available() const { return m_available; }
    [[nodiscard]] bool verbose() const;
    void setVerbose(bool verbose);
    [[nodiscard]] QString verboseEnds() const;
    [[nodiscard]] qint64 bytes() const { return m_bytes; }
    [[nodiscard]] qint64 oldBytes() const { return m_oldBytes; }
    [[nodiscard]] QVariantList files() const { return m_files; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openFolder() const;
    // Writes the named files (from `files`) into a zip at `target`; false when it
    // could not. A name not in the list is refused.
    Q_INVOKABLE bool exportTo(const QUrl& target, const QStringList& names);
    // A file name for the package: cirrus-diagnostics-20261109-1403.zip.
    Q_INVOKABLE static QString packageName();
    // Removes the files of earlier sessions.
    Q_INVOKABLE void clear();

  signals:
    void verboseChanged();
    void filesChanged();

  private:
    void applyVerbose();

    AppSettings* m_settings;
    QTimer m_expiry;
    bool m_available = false;
    qint64 m_bytes = 0;
    qint64 m_oldBytes = 0;
    QVariantList m_files;
    QStringList m_paths; // of m_files, same order
};

} // namespace cirrus::ui
