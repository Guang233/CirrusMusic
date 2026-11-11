#include "crash.h"
#include "log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <exception>
#include <typeinfo>
#include <utility>

#ifdef Q_OS_UNIX
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <elf.h>
#include <link.h>
#endif

namespace cirrus::log::crash
{
std::atomic<int> logFd{-1};

namespace
{
constexpr int kFrames = 64;

[[noreturn]] void onTerminate()
{
    // The exception's type only: its message may carry anything.
    QString type = QStringLiteral("none");
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            type = QString::fromLatin1(typeid(e).name());
        } catch (...) {
            type = QStringLiteral("unknown");
        }
    }
    qCCritical(lcApp) << event("app.terminate") << field("exception", type); // written at once
    std::abort(); // the signal handler writes the stack
}

#ifdef Q_OS_UNIX
char g_path[4096]; // the crash file, set before the handler is installed

void put(int fd, const char* text) { [[maybe_unused]] auto n = ::write(fd, text, strlen(text)); }

void putHex(int fd, quintptr value)
{
    char text[2 + 2 * sizeof(quintptr) + 2] = "0x";
    int n = 2;
    for (int shift = int(sizeof(quintptr)) * 8 - 4; shift >= 0; shift -= 4)
        text[n++] = "0123456789abcdef"[(value >> shift) & 0xf];
    text[n++] = '\n';
    [[maybe_unused]] auto written = ::write(fd, text, size_t(n));
}

void putNumber(int fd, int value)
{
    char digits[12];
    int d = 0;
    for (int v = value; v > 0 && d < 11; v /= 10)
        digits[d++] = char('0' + v % 10);
    char text[13];
    int n = 0;
    while (d > 0)
        text[n++] = digits[--d];
    text[n++] = '\n';
    [[maybe_unused]] auto written = ::write(fd, text, size_t(n));
}

// Async-signal-safe but for backtrace_symbols_fd(), which comes last: the log line, the
// frames, the modules, then the names the dynamic symbol tables know.
void onFatalSignal(int signal)
{
    if (const int fd = logFd.load(); fd >= 0) {
        put(fd, "fatal signal ");
        putNumber(fd, signal);
    }
    void* frames[kFrames];
    const int count = backtrace(frames, kFrames);
    const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        put(fd, "signal ");
        putNumber(fd, signal);
        for (int i = 0; i < count; ++i) {
            put(fd, "frame ");
            putHex(fd, quintptr(frames[i]));
        }
        put(fd, "maps\n");
        if (const int maps = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC); maps >= 0) {
            char buffer[4096];
            for (ssize_t n; (n = ::read(maps, buffer, sizeof buffer)) > 0;) [[maybe_unused]]
                auto written = ::write(fd, buffer, size_t(n));
            ::close(maps);
        }
        put(fd, "symbols\n");
        backtrace_symbols_fd(frames, count, fd);
        ::close(fd);
    }
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
#endif

struct Region {
    quint64 start, end, offset;
    QString module;
};
} // namespace

void install(const QString& path)
{
#ifdef Q_OS_UNIX
    // The path follows each session; the handlers are installed once.
    const QByteArray native = QFile::encodeName(path);
    if (native.size() >= qsizetype(sizeof g_path))
        return;
    std::memcpy(g_path, native.constData(), size_t(native.size()) + 1);
#else
    Q_UNUSED(path);
#endif
    static bool done = false;
    if (std::exchange(done, true))
        return;
    std::set_terminate(onTerminate);
#ifdef Q_OS_UNIX
    void* warm[1];
    backtrace(warm, 1); // loads what backtrace() needs now, not in the handler
    struct sigaction action{};
    action.sa_handler = onFatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(signal, &action, nullptr);
#endif
}

void report(const QString& directory)
{
    static const QRegularExpression fileName(QStringLiteral(R"(^crash-(cirrus-[\d-]+)\.txt$)"));
    // 7f3a1c000000-7f3a1c100000 r-xp 00012000 fd:01 1234 /usr/lib/libQt6Core.so.6.12.0
    static const QRegularExpression mapLine(
        QStringLiteral(R"(^([0-9a-f]+)-([0-9a-f]+) (\S{4}) ([0-9a-f]+) \S+ \d+\s+(/\S.*)$)"));
    // /usr/lib/libQt6Core.so.6(_ZN7QObject5eventEP6QEvent+0x2a) [0x7f3a1c0123ab]
    static const QRegularExpression symbolLine(QStringLiteral(R"(\(([^)]*)\))"));
    const auto entries =
        QDir(directory).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files);
    for (const QFileInfo& info : entries) {
        const auto name = fileName.match(info.fileName());
        QFile file(info.filePath());
        if (!name.hasMatch() || !file.open(QIODevice::ReadOnly))
            continue;
        int signal = 0;
        QList<quint64> frames;
        QList<Region> regions;
        QStringList symbols;
        enum { Frames, Maps, Symbols } part = Frames;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line == u"maps")
                part = Maps;
            else if (line == u"symbols")
                part = Symbols;
            else if (part == Frames && line.startsWith(u"signal "))
                signal = line.mid(7).toInt();
            else if (part == Frames && line.startsWith(u"frame "))
                frames.append(line.mid(6).toULongLong(nullptr, 16));
            else if (part == Maps) {
                const auto m = mapLine.match(line);
                if (m.hasMatch() && m.captured(3).contains(u'x'))
                    regions.append({m.captured(1).toULongLong(nullptr, 16),
                                    m.captured(2).toULongLong(nullptr, 16),
                                    m.captured(4).toULongLong(nullptr, 16),
                                    QFileInfo(m.captured(5)).fileName()});
            } else if (part == Symbols) {
                const auto m = symbolLine.match(line);
                const QString symbol = m.hasMatch() ? m.captured(1) : QString();
                symbols.append(symbol.startsWith(u'+') ? QString() : symbol);
            }
        }
        file.close();
        // The crashed session's build, from its log's first record, for symbolizing.
        QString build;
        if (QFile log(QDir(directory).filePath(name.captured(1) + QStringLiteral(".log")));
            log.open(QIODevice::ReadOnly)) {
            static const QRegularExpression id(QStringLiteral(R"(build_id=([0-9a-f]+))"));
            build = id.match(QString::fromUtf8(log.readLine())).captured(1);
        }
        qCWarning(lcApp) << event("app.crash") << field("session", name.captured(1))
                         << field("signal", signal) << field("frames", frames.size())
                         << fieldIf(!build.isEmpty(), "build_id", build);
        // crashes/<session>.txt for the diagnostic package: the same, as a table.
        QString report = QStringLiteral("session: %1\nsignal: %2\nbuild_id: %3\n\n")
                             .arg(name.captured(1))
                             .arg(signal)
                             .arg(build.isEmpty() ? QStringLiteral("-") : build);
        for (qsizetype i = 0; i < frames.size(); ++i) {
            const quint64 address = frames[i];
            const auto region =
                std::find_if(regions.cbegin(), regions.cend(), [address](const Region& r) {
                    return address >= r.start && address < r.end;
                });
            const bool known = region != regions.cend();
            const QString module = known ? region->module : QStringLiteral("?");
            const QString offset =
                QStringLiteral("0x") +
                QString::number(known ? address - region->start + region->offset : address, 16);
            qCInfo(lcApp) << event("app.crash_frame") << field("i", i) << field("module", module)
                          << field("offset", offset)
                          << fieldIf(!symbols.value(i).isEmpty(), "symbol", symbols.value(i));
            report += QStringLiteral("#%1 %2+%3 %4\n")
                          .arg(i, 2)
                          .arg(module, offset, symbols.value(i))
                          .trimmed() +
                      u'\n';
        }
        QDir(directory).mkpath(QStringLiteral("crashes"));
        if (QFile out(QDir(directory).filePath(QStringLiteral("crashes/") + name.captured(1) +
                                               QStringLiteral(".txt")));
            out.open(QIODevice::WriteOnly)) {
            out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            out.write(report.toUtf8());
        }
        QFile::remove(info.filePath());
    }
    // The newest ten crash reports stay.
    const auto kept = QDir(directory + QStringLiteral("/crashes"))
                          .entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Time);
    for (qsizetype i = 10; i < kept.size(); ++i)
        QFile::remove(kept[i].filePath());
}

QString buildId()
{
#ifdef Q_OS_LINUX
    QString id;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            // The first object is the executable.
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr) & header = info->dlpi_phdr[i];
                if (header.p_type != PT_NOTE)
                    continue;
                const auto* at = reinterpret_cast<const char*>(info->dlpi_addr + header.p_vaddr);
                const char* end = at + header.p_memsz;
                while (at + sizeof(ElfW(Nhdr)) <= end) {
                    const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(at);
                    const char* name = at + sizeof(ElfW(Nhdr));
                    const char* desc = name + ((note->n_namesz + 3) & ~3u);
                    if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 &&
                        std::memcmp(name, "GNU", 4) == 0) {
                        *static_cast<QString*>(data) =
                            QString::fromLatin1(QByteArray(desc, int(note->n_descsz)).toHex());
                        return 1;
                    }
                    at = desc + ((note->n_descsz + 3) & ~3u);
                }
            }
            return 1;
        },
        &id);
    return id;
#else
    return {};
#endif
}
} // namespace cirrus::log::crash
