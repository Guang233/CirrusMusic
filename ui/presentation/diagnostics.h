#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QQuickWindow;

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
    // Newest first: {name, bytes, modified (ms since epoch), current, suggested}; suggested:
    // this session, the one before, and any with a mark or that did not end (§5.2).
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
    // Writes summary.txt, the user's description (when there is one) and the named files
    // (from `files`) into a zip at `target`; false when it could not. A name not in the
    // list is refused.
    Q_INVOKABLE bool exportTo(const QUrl& target, const QStringList& names,
                              const QString& description = {});
    // summary.txt for the named files (logging-v2 §5.1), shown before exporting: the
    // app, the system, the network and audio setup, the account's state, the settings,
    // and each session's start, end, warnings and marks. No paths, hosts or ids.
    Q_INVOKABLE QString summary(const QStringList& names) const;
    // A file name for the package: cirrus-diagnostics-20261109-1403.zip.
    Q_INVOKABLE static QString packageName();
    // Removes the files of earlier sessions.
    Q_INVOKABLE void clear();

    // The user's trail (docs/tasks/logging-v2.md §4): the shell calls these, the log
    // gets nav.page, ui.popup, ui.notice, player.shown, and the context of warnings.
    // `page` is the page shown at `depth` (1 is the first), `properties` what it was
    // made with: its ids (…Key) are logged, nothing else.
    Q_INVOKABLE void pageShown(QObject* page, int depth, const QVariantMap& properties);
    Q_INVOKABLE void playerShown(bool shown);
    // A notice shown from QML (C++ notices use noticed(), notice.h).
    Q_INVOKABLE void notice(const QString& id);
    // "Something just went wrong" (§4.4): user.mark, the state of the UI, and the flight
    // recorder written to the file; then `marked`.
    Q_INVOKABLE void mark();

    // Watches which popup has the keyboard focus (dialogs and menus take it when they
    // open), and where the window is for ui.state.
    void watch(QQuickWindow* window);
    // A QML object's type as the log names it: its objectName, else its type without
    // the QML engine's suffix (AlbumPage_QMLTYPE_12 -> AlbumPage).
    static QString typeName(const QObject* object);
    // The ids that say which page or model it is: …Key properties (a user's hashed),
    // shelf, feed, date, tab, target, kind, category; "-" without any. Never anything
    // else, so no title or user text comes along.
    static QString ids(const QVariantMap& properties);
    static QString ids(const QObject* object, const QMetaObject& base);

    // Takes over the engine's warnings (logging-v2 §3): cirrus.ui.qml qml.warning with the
    // file and line, and the page the user was on. A cover the backend already logged as
    // failed is only a debug record.
    void watch(QQmlEngine* engine);

  signals:
    void verboseChanged();
    void filesChanged();
    void marked();

  private:
    void applyVerbose();
    void logState(const char* reason) const;
    void focusMoved();

    AppSettings* m_settings;
    QTimer m_expiry;
    bool m_available = false;
    qint64 m_bytes = 0;
    qint64 m_oldBytes = 0;
    QVariantList m_files;
    QStringList m_paths; // of m_files, same order
    QPointer<QQuickWindow> m_window;
    QString m_gpu;       // GL_RENDERER, set once on the render thread before any read
    QStringList m_pages; // HomePage>AlbumPage:123, bottom first
    QString m_popup;
    bool m_player = false;
    int m_marks = 0;
};

} // namespace cirrus::ui
