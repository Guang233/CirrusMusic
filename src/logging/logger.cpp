#include "logger.h"
#include "log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageAuthenticationCode>
#include <QMutex>
#include <QRandomGenerator>
#include <QRecursiveMutex>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <thread>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace cirrus::log
{
namespace
{
constexpr qsizetype kQueueLimit = 8192; // records waiting for the writer; more are dropped
constexpr qsizetype kHeldLimit = 8192;  // records held before start()
constexpr qsizetype kRecentLimit = 2000;
constexpr int kRepeatAllowed = 20;       // the same record this often within the window, then
constexpr qint64 kRepeatWindowMs = 1000; // the rest of the run is counted, not written
constexpr qsizetype kValueLimit = 200;
const QRegularExpression kFileName(QStringLiteral(R"(^(cirrus-\d{8}-\d{6}-\d+)(?:-(\d+))?\.log$)"));

// The file descriptor the crash handler writes to; -1 when no file is open.
std::atomic<int> g_crashFd{-1};

struct State {
    // The queue and everything the handler touches.
    QMutex mutex;
    QWaitCondition wake;
    std::deque<QByteArray> queue;
    std::deque<QByteArray> recent;
    qint64 dropped = 0;
    QByteArray lastCategory;
    QString lastMessage;
    int repeats = 0;
    qint64 repeatStart = 0;
    bool suppressing = false;
    qint64 suppressed = 0;
    bool stopping = false;
    QElapsedTimer clock;

    bool installed = false;
    bool console = true;
    bool verbose = false;
    QtMessageHandler previous = nullptr;
    std::thread writer;

    // The file. Taken before `mutex`, never the other way round.
    QRecursiveMutex fileMutex;
    QFile file;
    QList<QByteArray> held;
    bool started = false;
    FileOptions options;
    QString prefix;         // cirrus-YYYYMMDD-HHMMSS-PID
    QString previousPrefix; // the session before, kept with this one
    int part = 1;
    qint64 written = 0;

    // log::path() and log::account().
    QMutex valuesMutex;
    QList<std::pair<QString, QString>> roots; // longest first
    QByteArray accountKey;
};

State& state()
{
    static auto* s = new State; // never destroyed: records may come during exit
    return *s;
}

char levelLetter(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 'D';
    case QtInfoMsg:
        return 'I';
    case QtWarningMsg:
        return 'W';
    case QtCriticalMsg:
        return 'C';
    case QtFatalMsg:
        return 'F';
    }
    return '?';
}

QString threadName()
{
    QThread* thread = QThread::currentThread();
    QString name = thread ? thread->objectName() : QString();
    if (name.isEmpty())
        return u't' + QString::number(quintptr(QThread::currentThreadId()), 16);
    name.replace(u' ', u'-');
    return name;
}

// Control characters escaped, so a record is always one line.
void appendEscaped(QString& out, QStringView text)
{
    for (const QChar c : text) {
        if (c == u'\n')
            out += u"\\n";
        else if (c == u'\r')
            out += u"\\r";
        else if (c.unicode() < 0x20 && c != u'\t')
            out += u"\\x" + QString::number(c.unicode(), 16).rightJustified(2, u'0');
        else
            out += c;
    }
}

// What a record from anywhere (Qt, QML, a library) must not carry, whatever its author
// thought (§5): local roots and the home directory, URL queries and fragments.
QString scrub(QString message)
{
    if (message.contains(u"://")) {
        static const QRegularExpression query(
            QStringLiteral(R"(([A-Za-z][A-Za-z0-9+.\-]*://[^\s?#"'<>]*)[?#][^\s"'<>]*)"));
        message.replace(query, QStringLiteral("\\1?…"));
    }
    auto& s = state();
    {
        QMutexLocker lock(&s.valuesMutex);
        for (const auto& [placeholder, root] : std::as_const(s.roots))
            message.replace(root, placeholder);
    }
    const QString home = QDir::homePath();
    if (home.size() > 1)
        message.replace(home, QStringLiteral("~"));
    return message;
}

QByteArray format(QtMsgType type, QByteArrayView category, const QString& message)
{
    // Local time with its offset: 2026-11-09T14:03:12.345+08:00.
    const QDateTime now = QDateTime::currentDateTime();
    const int offset = now.offsetFromUtc() / 60;
    QString line = now.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
    line += offset < 0 ? u'-' : u'+';
    line += QString::number(qAbs(offset) / 60).rightJustified(2, u'0') + u':' +
            QString::number(qAbs(offset) % 60).rightJustified(2, u'0');
    line += u' ';
    line += QChar::fromLatin1(levelLetter(type));
    line += u' ';
    line += threadName().leftJustified(7);
    line += u' ';
    line += QString::fromLatin1(category);
    line += u' ';
    appendEscaped(line, scrub(message));
    return line.toUtf8();
}

QByteArray notice(const char* name, qint64 count)
{
    return format(QtInfoMsg, "cirrus.log",
                  QString::fromLatin1(name) + QStringLiteral(" count=") + QString::number(count));
}

// With `mutex` held.
void enqueue(State& s, QByteArray line)
{
    if (qsizetype(s.queue.size()) >= kQueueLimit) {
        ++s.dropped;
        return;
    }
    if (s.dropped) {
        s.queue.push_back(notice("log.dropped", std::exchange(s.dropped, 0)));
    }
    s.recent.push_back(line);
    if (qsizetype(s.recent.size()) > kRecentLimit)
        s.recent.pop_front();
    s.queue.push_back(std::move(line));
}

// With `mutex` held.
void endRepeats(State& s)
{
    if (s.suppressed)
        enqueue(s, notice("log.repeated", std::exchange(s.suppressed, 0)));
    s.suppressing = false;
}

void writeHeader(State& s);
void prune(State& s);

// With `fileMutex` held.
bool openPart(State& s, int part)
{
    g_crashFd.store(-1);
    s.file.close();
    s.part = part;
    const QString name =
        s.prefix + (part > 1 ? u'-' + QString::number(part) : QString()) + QStringLiteral(".log");
    s.file.setFileName(QDir(s.options.directory).filePath(name));
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered))
        return false;
    s.file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    s.written = s.file.size();
    g_crashFd.store(s.file.handle());
    writeHeader(s);
    return true;
}

// With `fileMutex` held.
void writeFile(State& s, const QByteArray& blob)
{
    if (blob.isEmpty() || !s.file.isOpen())
        return;
    s.file.write(blob);
    s.written += blob.size();
}

// With `fileMutex` held.
void writeLines(State& s, const std::deque<QByteArray>& lines)
{
    if (s.console) {
        for (const QByteArray& line : lines) {
            std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
            std::fputc('\n', stderr);
        }
        std::fflush(stderr);
    }
    if (!s.started) {
        for (const QByteArray& line : lines)
            if (s.held.size() < kHeldLimit)
                s.held.append(line);
        return;
    }
    QByteArray blob;
    for (const QByteArray& line : lines) {
        if (s.written + blob.size() + line.size() + 1 > s.options.fileLimit &&
            s.written + blob.size() > 0) {
            writeFile(s, blob);
            blob.clear();
            if (!openPart(s, s.part + 1))
                return;
            prune(s);
        }
        blob += line;
        blob += '\n';
    }
    writeFile(s, blob);
}

// Writes what is queued now, on the calling thread.
void drain()
{
    auto& s = state();
    QMutexLocker fileLock(&s.fileMutex);
    std::deque<QByteArray> lines;
    {
        QMutexLocker lock(&s.mutex);
        lines.swap(s.queue);
    }
    if (!lines.empty())
        writeLines(s, lines);
}

void writerLoop()
{
    auto& s = state();
    for (;;) {
        {
            QMutexLocker lock(&s.mutex);
            while (s.queue.empty() && !s.stopping)
                s.wake.wait(&s.mutex);
            if (s.queue.empty() && s.stopping)
                return;
        }
        drain();
    }
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    thread_local bool inside = false;
    if (inside) { // a record while writing one: straight to stderr
        std::fprintf(stderr, "%s\n", qUtf8Printable(message));
        return;
    }
    inside = true;
    auto& s = state();
    const QByteArrayView category = context.category ? context.category : "default";
    const bool urgent = type == QtCriticalMsg || type == QtFatalMsg;
    QByteArray line = format(type, category, message); // outside the lock
    {
        QMutexLocker lock(&s.mutex);
        const bool same = s.lastCategory == category && s.lastMessage == message;
        const qint64 now = s.clock.elapsed();
        if (!urgent && same && s.suppressing) {
            ++s.suppressed;
            inside = false;
            return;
        }
        if (!same) {
            endRepeats(s);
            s.lastCategory = category.toByteArray();
            s.lastMessage = message;
            s.repeats = 0;
            s.repeatStart = now;
        } else if (now - s.repeatStart >= kRepeatWindowMs) {
            s.repeats = 0;
            s.repeatStart = now;
        }
        if (!urgent && ++s.repeats > kRepeatAllowed) {
            s.suppressing = true;
            ++s.suppressed;
            inside = false;
            return;
        }
        enqueue(s, std::move(line));
    }
    if (urgent)
        drain(); // on the file before the process may go down
    else
        s.wake.wakeOne();
    inside = false;
}

// With `fileMutex` held.
void writeHeader(State& s)
{
    QString text = QStringLiteral("app.start");
    const auto add = [&text](const char* key, const QString& value) {
        text += u' ' + QString::fromLatin1(key) + u'=' + quoted(value);
    };
    add("version", QCoreApplication::applicationVersion());
#ifdef QT_NO_DEBUG
    add("build", QStringLiteral("release"));
#else
    add("build", QStringLiteral("debug"));
#endif
    add("qt", QString::fromLatin1(qVersion()));
    add("os", QSysInfo::prettyProductName());
    add("kernel", QSysInfo::kernelVersion());
    add("arch", QSysInfo::currentCpuArchitecture());
    add("locale", QLocale::system().name());
    add("pid", QString::number(QCoreApplication::applicationPid()));
    add("part", QString::number(s.part));
    if (!s.options.header.isEmpty())
        text += u' ' + s.options.header;
    QByteArray line = format(QtInfoMsg, "cirrus.app", text);
    line += '\n';
    writeFile(s, line);
}

struct FileEntry {
    QFileInfo info;
    QString prefix;
};

QList<FileEntry> logFiles(const QString& directory)
{
    QList<FileEntry> files;
    const auto infos = QDir(directory).entryInfoList({QStringLiteral("cirrus-*.log")}, QDir::Files,
                                                     QDir::Time | QDir::Reversed);
    for (const QFileInfo& info : infos) {
        const auto match = kFileName.match(info.fileName());
        if (match.hasMatch())
            files.append({info, match.captured(1)});
    }
    return files; // oldest first
}

// With `fileMutex` held. Old files go, then the oldest while over budget; this session's
// and the previous one's stay.
void prune(State& s)
{
    auto files = logFiles(s.options.directory);
    const auto kept = [&s](const FileEntry& f) {
        return f.prefix == s.prefix ||
               (!s.previousPrefix.isEmpty() && f.prefix == s.previousPrefix);
    };
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-s.options.retentionDays);
    qint64 total = 0;
    QList<FileEntry> remaining;
    for (const FileEntry& f : std::as_const(files)) {
        if (!kept(f) && f.info.lastModified() < cutoff && QFile::remove(f.info.filePath()))
            continue;
        remaining.append(f);
        total += f.info.size();
    }
    for (const FileEntry& f : std::as_const(remaining)) {
        if (total <= s.options.totalLimit)
            break;
        if (!kept(f) && QFile::remove(f.info.filePath()))
            total -= f.info.size();
    }
}

#ifdef Q_OS_UNIX
// Async-signal-safe: one write to the open file, then the default action.
void onFatalSignal(int signal)
{
    const int fd = g_crashFd.load();
    if (fd >= 0) {
        char text[32] = "fatal signal ";
        int n = 13;
        char digits[12];
        int d = 0;
        for (int v = signal; v > 0 && d < 11; v /= 10)
            digits[d++] = char('0' + v % 10);
        while (d > 0)
            text[n++] = digits[--d];
        text[n++] = '\n';
        [[maybe_unused]] const auto written = ::write(fd, text, size_t(n));
    }
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void installCrashHandler()
{
    static bool done = false;
    if (std::exchange(done, true))
        return;
    struct sigaction action{};
    action.sa_handler = onFatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(signal, &action, nullptr);
}
#else
void installCrashHandler() {}
#endif
} // namespace

QStringList detail::splitNames(const char* names)
{
    QStringList list;
    for (const QString& name : QString::fromLatin1(names).split(u','))
        list.append(name.trimmed());
    return list;
}

QString quoted(QStringView value)
{
    QString text =
        value.size() > kValueLimit ? value.left(kValueLimit).toString() + u'…' : value.toString();
    const bool plain = !text.isEmpty() && std::none_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.isSpace() || c == u'"' || c == u'=' || c == u'\\' || c.unicode() < 0x20;
    });
    if (plain)
        return text;
    QString out = QStringLiteral("\"");
    for (const QChar c : std::as_const(text)) {
        if (c == u'"' || c == u'\\')
            out += u'\\';
        out += c;
    }
    out += u'"';
    return out; // control characters are escaped with the whole record
}

Host host(const QUrl& url)
{
    const QString h = url.host().toLower();
    return {h.isEmpty() ? QStringLiteral("-") : h};
}

Path path(const QString& localPath)
{
    const QString p = QDir::cleanPath(QDir::fromNativeSeparators(localPath));
    auto& s = state();
    {
        QMutexLocker lock(&s.valuesMutex);
        for (const auto& [placeholder, root] : std::as_const(s.roots))
            if (p == root || p.startsWith(root + u'/'))
                return {placeholder + p.mid(root.size())};
    }
    return {QFileInfo(p).fileName()};
}

Account account(QStringView id)
{
    if (id.isEmpty())
        return {QStringLiteral("-")};
    auto& s = state();
    QByteArray key;
    {
        QMutexLocker lock(&s.valuesMutex);
        if (s.accountKey.isEmpty()) { // none given: stable for this run only
            s.accountKey.resize(32);
            QRandomGenerator::system()->generate(s.accountKey.begin(), s.accountKey.end());
        }
        key = s.accountKey;
    }
    const QByteArray mac =
        QMessageAuthenticationCode::hash(id.toUtf8(), key, QCryptographicHash::Sha256);
    return {QString::fromLatin1(mac.toHex().left(12))};
}

void setPathRoots(const QList<std::pair<QString, QString>>& roots)
{
    QList<std::pair<QString, QString>> clean;
    for (const auto& [placeholder, root] : roots)
        if (!root.isEmpty())
            clean.append({placeholder, QDir::cleanPath(QDir::fromNativeSeparators(root))});
    std::sort(clean.begin(), clean.end(),
              [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    auto& s = state();
    QMutexLocker lock(&s.valuesMutex);
    s.roots = std::move(clean);
}

void setAccountKey(const QByteArray& key)
{
    auto& s = state();
    QMutexLocker lock(&s.valuesMutex);
    s.accountKey = key;
}

void install(bool console)
{
    auto& s = state();
    if (s.installed)
        return;
    // Qt names it "Qt mainThread"; a short name keeps the column narrow.
    if (QThread* main = QThread::currentThread();
        main && (main->objectName().isEmpty() || main->objectName() == u"Qt mainThread"))
        main->setObjectName(QStringLiteral("main"));
    {
        QMutexLocker lock(&s.mutex);
        s.console = console;
        s.stopping = false;
        s.clock.start();
    }
    setVerbose(false);
    s.previous = qInstallMessageHandler(handler);
    s.writer = std::thread(writerLoop);
    s.installed = true;
}

bool start(const FileOptions& options)
{
    auto& s = state();
    QString unclean;
    {
        QMutexLocker fileLock(&s.fileMutex);
        if (s.started)
            return true;
        s.options = options;
        QDir dir(options.directory);
        if (options.directory.isEmpty() || !dir.mkpath(QStringLiteral(".")))
            return false;
        QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                      QFileDevice::ExeOwner);
        s.prefix =
            QStringLiteral("cirrus-%1-%2")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                .arg(QCoreApplication::applicationPid());
        // The previous session: the newest file of another session.
        const auto files = logFiles(options.directory);
        s.previousPrefix.clear();
        for (auto it = files.crbegin(); it != files.crend(); ++it) {
            if (it->prefix == s.prefix)
                continue;
            s.previousPrefix = it->prefix;
            QFile last(it->info.filePath());
            if (last.open(QIODevice::ReadOnly)) {
                last.seek(qMax<qint64>(0, last.size() - 4096));
                const QByteArray tail = last.readAll();
                if (!tail.contains(" app.exit")) {
                    unclean = it->info.fileName();
                    const qsizetype at = tail.lastIndexOf("fatal signal ");
                    if (at >= 0)
                        unclean += u' ' + QString::fromLatin1(tail.mid(at).split('\n').first())
                                              .replace(QStringLiteral("fatal signal "),
                                                       QStringLiteral("signal="));
                }
            }
            break;
        }
        if (!openPart(s, 1))
            return false;
        QByteArray blob;
        for (const QByteArray& line : std::as_const(s.held)) {
            blob += line;
            blob += '\n';
        }
        s.held.clear();
        writeFile(s, blob);
        s.started = true;
        prune(s);
    }
    installCrashHandler();
    if (!unclean.isEmpty()) {
        const auto parts = unclean.split(u' ');
        if (parts.size() > 1)
            qCWarning(lcApp) << event("app.previous_session_unclean") << field("file", parts[0])
                             << Text{parts[1]};
        else
            qCWarning(lcApp) << event("app.previous_session_unclean") << field("file", parts[0]);
    }
    return true;
}

void flush() { drain(); }

void shutdown()
{
    auto& s = state();
    if (!s.installed)
        return;
    qCInfo(lcApp) << event("app.exit");
    {
        QMutexLocker lock(&s.mutex);
        endRepeats(s);
        s.stopping = true;
    }
    s.wake.wakeAll();
    s.writer.join();
    drain();
    qInstallMessageHandler(s.previous);
    {
        QMutexLocker fileLock(&s.fileMutex);
        g_crashFd.store(-1);
        s.file.close();
        s.started = false;
        s.held.clear();
    }
    QMutexLocker lock(&s.mutex);
    s.recent.clear();
    s.lastCategory.clear();
    s.lastMessage.clear();
    s.installed = false;
}

void setVerbose(bool verbose)
{
    auto& s = state();
    {
        QMutexLocker lock(&s.mutex);
        s.verbose = verbose;
    }
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("*.debug=false\ncirrus.*.debug=true")
                                             : QStringLiteral("*.debug=false"));
}

bool verbose()
{
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    return s.verbose;
}

QList<LogFile> files()
{
    auto& s = state();
    QMutexLocker fileLock(&s.fileMutex);
    QList<LogFile> list;
    if (!s.started)
        return list;
    const auto found = logFiles(s.options.directory);
    for (auto it = found.crbegin(); it != found.crend(); ++it)
        list.append({it->info.filePath(), it->info.fileName(), it->info.size(),
                     it->info.lastModified(), it->prefix == s.prefix});
    return list;
}

qint64 clearOld()
{
    auto& s = state();
    QMutexLocker fileLock(&s.fileMutex);
    if (!s.started)
        return 0;
    qint64 freed = 0;
    for (const FileEntry& f : logFiles(s.options.directory))
        if (f.prefix != s.prefix && QFile::remove(f.info.filePath()))
            freed += f.info.size();
    s.previousPrefix.clear();
    return freed;
}

QString standardDirectory(const QString& app)
{
#if defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Logs/") + app;
#elif defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' + app +
           QStringLiteral("/logs");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + u'/' + app +
           QStringLiteral("/logs");
#endif
}

QString directory()
{
    auto& s = state();
    QMutexLocker lock(&s.fileMutex);
    return s.started ? s.options.directory : QString();
}

QString currentFile()
{
    auto& s = state();
    QMutexLocker lock(&s.fileMutex);
    return s.file.isOpen() ? s.file.fileName() : QString();
}

QStringList recent()
{
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    QStringList lines;
    lines.reserve(qsizetype(s.recent.size()));
    for (const QByteArray& line : s.recent)
        lines.append(QString::fromUtf8(line));
    return lines;
}
} // namespace cirrus::log
