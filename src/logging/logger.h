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

// Debug records of the cirrus.* categories (§7). QT_LOGGING_RULES still wins.
void setVerbose(bool verbose);
bool verbose();

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
