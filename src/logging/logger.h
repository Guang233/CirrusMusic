#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

// The process's log (docs/tasks/logging.md §3): one Qt message handler takes every record
// (Qt's own, QML's, ours), formats it on the calling thread and queues it; a writer thread
// writes it to stderr and, once start() has opened one, to the session's log file.
namespace cirrus::log
{
struct FileOptions {
    QString directory;                    // created private (0700) when missing
    QString header;                       // extra key=value fields for app.start
    qint64 fileLimit = 4 * 1024 * 1024;   // a session continues in a new file after this
    qint64 totalLimit = 20 * 1024 * 1024; // all files; the oldest go first
    int retentionDays = 7;                // older files go
};

// Installs the handler; records are held until start(). Call it first in main(); the
// process logs through it until shutdown().
void install(bool console = true);
// Opens this session's file, writes the records held so far, removes old files, and
// notes when the previous session did not end cleanly. False if the directory or the
// file can't be made (the log stays on stderr).
bool start(const FileOptions& options);
// Writes app.exit, drains everything, closes the file and puts Qt's handler back.
void shutdown();
// Waits until the records logged so far are written (tests, before export).
void flush();

// Debug records of the cirrus.* categories go to the file as they come (§7). When off they
// go to the flight recorder (docs/tasks/logging-v2.md §2): kept in memory, and written to
// the file with the 30 seconds before each warning, or all of them by writeContext().
// QT_LOGGING_RULES still wins.
void setVerbose(bool verbose);
bool verbose();
// Writes the flight recorder's records not in the file yet (a user's mark, an export),
// between log.context_begin and log.context_end lines naming `reason`.
void writeContext(const char* reason);

// Where the user is (logging-v2 §4.3), added to every warning as ctx.<key>=<value>:
// setContext("page", "AlbumPage"). Short identifiers only, never user text; an empty value removes
// the key.
void setContext(const char* key, const QString& value);

// Where `app`'s logs go (§6): $XDG_STATE_HOME/<app>/logs on Linux (~/.local/state),
// ~/Library/Logs/<app> on macOS, %LOCALAPPDATA%/<app>/logs on Windows.
QString standardDirectory(const QString& app);

// The log's files (the settings page, exporting), newest first; empty before start().
struct LogFile {
    QString path;
    QString name;
    qint64 bytes = 0;
    QDateTime modified;
    bool current = false; // this session's
};
QList<LogFile> files();
// Removes every file but this session's; the bytes freed.
qint64 clearOld();
// Writes `files` (paths from files()) into a zip at `target`, deflated; false when a file
// can't be read or the target can't be written (nothing half-written is left).
bool writeArchive(const QString& target, const QStringList& files);

QString directory();   // empty before start()
QString currentFile(); // the file written now
QStringList recent();  // the last records in memory, oldest first

// Roots that log::path() writes as placeholders, e.g. {"<cache>", "/home/u/.cache/Cirrus"}.
void setPathRoots(const QList<std::pair<QString, QString>>& roots);
// The key log::account() hashes with; stable per installation, never logged.
void setAccountKey(const QByteArray& key);

// install() at construction, shutdown() at destruction: declare it before the
// application object, so it is the last thing to go.
class Session
{
  public:
    explicit Session(bool console = true) { install(console); }
    ~Session() { shutdown(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};
} // namespace cirrus::log
