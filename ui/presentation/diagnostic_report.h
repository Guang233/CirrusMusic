#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

class QQuickWindow;

// The files of a diagnostic package besides the logs (docs/tasks/logging-v2.md §12), laid
// out like IntelliJ's "Collect Logs and Diagnostic Data" and Firefox's about:support: one
// plain-text file per subject, "[section]" headers and "key: value" lines. Nothing in them
// names the user: no account or person ids, no titles, no user text; paths only as
// placeholders (<data>, ~).
namespace cirrus::ui
{
class AppSettings;

namespace report
{
// What the UI knows when the package is made (Diagnostics keeps it).
struct UiState {
    QQuickWindow* window = nullptr;
    AppSettings* settings = nullptr;
    QStringList pages; // HomePage>AlbumPage:123, bottom first
    QString popup;
    bool player = false;
    QString graphics; // graphics.txt's device part, taken on the render thread
    QDateTime started;
};

QString about(const UiState& ui);
QString system(const UiState& ui);
QString graphics(const UiState& ui);
QString audio();
QString environment();
QString modules();
QString settings(const AppSettings* settings);
QString state(const UiState& ui);

// The renderer's identity, on the render thread right after the scene graph started:
// the RHI backend and device, and OpenGL's own strings when it is OpenGL.
QString graphicsDevice(QQuickWindow* window);

// cirrus-20261111-070345-230368-2.log -> cirrus-20261111-070345-230368
QString sessionOf(const QString& logFileName);

// What README.txt and the export dialog say about a log file.
struct LogScan {
    QString first, last;          // the first and last record's time
    QString header;               // the app.start record's fields
    bool ended = false;           // app.exit seen
    QStringList marks;            // user.mark times
    QString signal;               // "fatal signal 11"
    QHash<QString, int> warnings; // "cirrus.net request.failed" -> count
    int records = 0;
};
LogScan scanLog(const QString& path);

// README.txt: what the package holds and how it was made, then the sessions it covers.
QString readme(const QStringList& files, const QList<LogScan>& scans, const QStringList& logNames,
               bool description);
} // namespace report

// network.txt (§12): the proxy setup, the network's state, TLS, and for each NetEase host
// a DNS lookup and an HTTPS request through the system's proxy and directly, with times
// and Qt's words for what failed. Takes at most `timeoutMs`.
class NetworkProbe : public QObject
{
    Q_OBJECT
  public:
    explicit NetworkProbe(QObject* parent = nullptr);
    void start(int timeoutMs = 6000);

  signals:
    void finished(const QString& text);

  private:
    struct Host {
        QString name;
        QString dns;
        QString viaProxy;
        QString direct;
    };
    void request(int host, bool direct, int timeoutMs);
    void done();

    QNetworkAccessManager m_system;
    QNetworkAccessManager m_direct;
    QList<Host> m_hosts;
    int m_waiting = 0;
    bool m_finished = false;
};
} // namespace cirrus::ui
