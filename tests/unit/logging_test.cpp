#include "logging/log.h"
#include "logging/logger.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest>

namespace demo
{
enum class Plain { A, B, C };
enum class Named { Ready, Broken };
QString logName(Named n)
{
    return n == Named::Ready ? QStringLiteral("ready") : QStringLiteral("broken");
}
} // namespace demo

using namespace cirrus;

namespace
{
QString text(const log::Text& t) { return t.text; }

QString readAll(const QString& directory)
{
    QString all;
    auto files = QDir(directory).entryInfoList({QStringLiteral("cirrus-*.log")}, QDir::Files,
                                               QDir::Time | QDir::Reversed);
    for (const QFileInfo& info : std::as_const(files)) {
        QFile f(info.filePath());
        if (f.open(QIODevice::ReadOnly))
            all += QString::fromUtf8(f.readAll());
    }
    return all;
}

log::FileOptions options(const QTemporaryDir& dir)
{
    log::FileOptions o;
    o.directory = dir.filePath(QStringLiteral("logs"));
    o.header = QStringLiteral("commit=abc123");
    return o;
}

// A file of an earlier session, `days` old.
void oldFile(const QString& directory, const QString& name, qint64 bytes, int days,
             const QByteArray& last = " app.exit\n")
{
    QDir().mkpath(directory);
    QFile f(QDir(directory).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x') + '\n' + last);
    f.close();
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-days),
                          QFileDevice::FileModificationTime));
}
} // namespace

class LoggingTest : public QObject
{
    Q_OBJECT
  private slots:
    void cleanup() { log::shutdown(); }

    void fieldsAreSafeAndReadable()
    {
        QCOMPARE(text(log::event("load.failed")), "load.failed");
        QCOMPARE(text(log::field("ms", 203)), "ms=203");
        QCOMPARE(text(log::field("ok", true)), "ok=true");
        QCOMPARE(text(log::field("ratio", 0.5)), "ratio=0.5");
        QCOMPARE(text(log::field("op", QStringLiteral("song/url/v1"))), "op=song/url/v1");
        QCOMPARE(text(log::field("os", "Arch Linux")), "os=\"Arch Linux\"");
        QCOMPARE(text(log::field("v", QStringLiteral("a\"b\\c"))), "v=\"a\\\"b\\\\c\"");
        QCOMPARE(text(log::field("v", QString())), "v=\"\"");
        QCOMPARE(text(log::field("v", QStringLiteral("a=b"))), "v=\"a=b\"");
        QCOMPARE(text(log::field("key", Qt::Key_A)), "key=Key_A"); // Q_ENUM names
        QCOMPARE(text(log::field("e", demo::Plain::C)), "e=2");
        QCOMPARE(text(log::field("e", demo::Named::Broken)), "e=broken"); // logName()
        const QString longValue(500, u'a');
        QCOMPARE(text(log::field("v", longValue)).size(), 2 + 200 + 1);
    }

    void safeForms()
    {
        QCOMPARE(log::host(QUrl("https://M10.music.126.net/a/b.flac?vuutv=secret")).value,
                 "m10.music.126.net");
        QCOMPARE(log::host(QUrl()).value, "-");
        log::setPathRoots(
            {{"<cache>", "/home/u/.cache/Cirrus"}, {"<audio>", "/home/u/.cache/Cirrus/audio"}});
        QCOMPARE(log::path("/home/u/.cache/Cirrus/audio/12.flac").value, "<audio>/12.flac");
        QCOMPARE(log::path("/home/u/.cache/Cirrus/resources/a").value, "<cache>/resources/a");
        QCOMPARE(log::path("/srv/music/song.mp3").value, "song.mp3");
        log::setAccountKey("k1");
        const auto a = log::account(u"101");
        QCOMPARE(a.value.size(), 12);
        QCOMPARE(log::account(u"101").value, a.value);
        QVERIFY(log::account(u"102").value != a.value);
        QVERIFY(!a.value.contains("101"));
        log::setAccountKey("k2");
        QVERIFY(log::account(u"101").value != a.value);
        QCOMPARE(log::account(u"").value, "-");
        log::setPathRoots({});
    }

    void sessionFile()
    {
        QTemporaryDir dir;
        log::install(false);
        qCInfo(lcPlayback) << log::event("held") << log::field("n", 1); // before the file
        QVERIFY(log::currentFile().isEmpty());
        QVERIFY(log::start(options(dir)));
        QVERIFY(QFileInfo(log::currentFile()).fileName().startsWith("cirrus-"));
        qCWarning(lcNet) << log::event("request.failed") << log::field("req", 7);
        qWarning("two\nlines");
        qWarning("see https://interface.music.163.com/eapi/x?token=secret#frag now");
        qWarning("%s", qPrintable(QDir::homePath() + "/private/file"));
        qCDebug(lcMedia) << "hidden by default";
        log::setVerbose(true);
        qCDebug(lcMedia) << log::event("shown");
        log::setVerbose(false);
        log::flush();
        const QString file = log::currentFile();
        log::shutdown();

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
        // The header first, then what was held, then the rest; each record one line.
        QVERIFY2(lines.value(0).contains(" I main    cirrus.app app.start version="),
                 qPrintable(lines.value(0)));
        QVERIFY(lines.value(0).contains("commit=abc123"));
        QVERIFY(lines.value(0).contains("part=1"));
        static const QRegularExpression record("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3}"
                                               "(Z|[+-]\\d\\d:\\d\\d) [DIWCF] \\S+\\s+\\S+ .+$");
        for (const QString& line : lines)
            QVERIFY2(record.match(line).hasMatch(), qPrintable(line));
        QVERIFY(lines.value(1).endsWith("cirrus.playback held n=1"));
        QVERIFY(lines.value(2).endsWith(" W main    cirrus.net request.failed req=7"));
        QVERIFY(lines.value(3).endsWith("default two\\nlines"));
        QVERIFY2(lines.value(4).endsWith("see https://interface.music.163.com/eapi/x?… now"),
                 qPrintable(lines.value(4)));
        QVERIFY(lines.value(5).endsWith("~/private/file"));
        QVERIFY(lines.value(6).endsWith(" D main    cirrus.media shown"));
        QVERIFY(lines.last().endsWith("cirrus.app app.exit"));
        const QString all = lines.join(u'\n');
        QVERIFY(!all.contains("secret"));
        QVERIFY(!all.contains("hidden by default"));
        QCOMPARE(QFileInfo(dir.filePath("logs")).permissions() & QFileDevice::ReadOther,
                 QFileDevice::Permissions{});
        QCOMPARE(QFileInfo(file).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions{});
    }

    void previousSessionThatDidNotEnd()
    {
        QTemporaryDir dir;
        const auto o = options(dir);
        oldFile(o.directory, "cirrus-20261101-100000-11.log", 10, 2);
        oldFile(o.directory, "cirrus-20261108-100000-12.log", 10, 1,
                "... I main    cirrus.app app.start\nfatal signal 11\n");
        log::install(false);
        QVERIFY(log::start(o));
        log::flush();
        const auto recent = log::recent();
        QVERIFY(std::any_of(recent.cbegin(), recent.cend(), [](const QString& line) {
            return line.endsWith(
                "app.previous_session_unclean file=cirrus-20261108-100000-12.log signal=11");
        }));
        log::shutdown();
        // A previous session that ended: no complaint.
        oldFile(o.directory, "cirrus-20261109-100000-13.log", 10, 0);
        log::install(false);
        QVERIFY(log::start(o));
        log::flush();
        const auto again = log::recent();
        QVERIFY(std::none_of(again.cbegin(), again.cend(),
                             [](const QString& line) { return line.contains("unclean"); }));
    }

    void partsAndRetention()
    {
        QTemporaryDir dir;
        auto o = options(dir);
        o.fileLimit = 4000;
        o.totalLimit = 30000;
        oldFile(o.directory, "cirrus-20261001-100000-1.log", 100, 30);  // too old
        oldFile(o.directory, "cirrus-20261105-100000-2.log", 20000, 4); // over budget, oldest
        oldFile(o.directory, "cirrus-20261106-100000-3.log", 5000, 3);  // kept within budget
        oldFile(o.directory, "cirrus-20261107-100000-4.log", 9000, 2);  // the previous session
        oldFile(o.directory, "cirrus-20261107-100000-4-2.log", 9000, 2);
        log::install(false);
        QVERIFY(log::start(o));
        for (int i = 0; i < 200; ++i)
            qCInfo(lcApp) << log::event("filler") << log::field("i", i) << QString(40, u'y');
        log::flush();
        QVERIFY(QFileInfo(log::currentFile())
                    .fileName()
                    .contains(QRegularExpression("-\\d+-\\d+\\.log$")));
        const QDir logs(o.directory);
        QVERIFY(!logs.exists("cirrus-20261001-100000-1.log"));
        QVERIFY(!logs.exists("cirrus-20261105-100000-2.log"));
        QVERIFY(logs.exists("cirrus-20261107-100000-4.log"));
        QVERIFY(logs.exists("cirrus-20261107-100000-4-2.log"));
        // Every part of this session starts with the header, and nothing was lost.
        int parts = 0;
        int records = 0;
        for (const QFileInfo& info : logs.entryInfoList({"cirrus-*.log"}, QDir::Files)) {
            if (info.fileName().startsWith("cirrus-202611") && info.fileName().contains("-1000"))
                continue; // the fakes
            QFile f(info.filePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString content = QString::fromUtf8(f.readAll());
            QVERIFY(content.section(u'\n', 0, 0).contains("app.start"));
            QVERIFY(info.size() <= o.fileLimit + 200);
            records += content.count(" filler i=");
            ++parts;
        }
        QVERIFY2(parts > 3, qPrintable(QString::number(parts)));
        QCOMPARE(records, 200);
    }

    void repeatsAreCounted()
    {
        QTemporaryDir dir;
        log::install(false);
        QVERIFY(log::start(options(dir)));
        for (int i = 0; i < 100; ++i)
            qCWarning(lcMedia) << log::event("same");
        qCWarning(lcMedia) << log::event("different");
        log::shutdown();
        const QString all = readAll(dir.filePath("logs"));
        QCOMPARE(all.count("cirrus.media same"), 20);
        QVERIFY(all.contains("cirrus.log log.repeated count=80\n"));
        QVERIFY(all.indexOf("log.repeated") < all.indexOf("cirrus.media different"));
    }

    void manyThreads()
    {
        QTemporaryDir dir;
        log::install(false);
        QVERIFY(log::start(options(dir)));
        QList<QThread*> threads;
        for (int t = 0; t < 4; ++t) {
            threads.append(QThread::create([t] {
                for (int i = 0; i < 500; ++i)
                    qCInfo(lcPlayback)
                        << log::event("tick") << log::field("t", t) << log::field("i", i);
            }));
            threads.last()->setObjectName(QStringLiteral("worker%1").arg(t));
            threads.last()->start();
        }
        for (QThread* thread : std::as_const(threads)) {
            QVERIFY(thread->wait(10000));
            delete thread;
        }
        log::shutdown();
        const QString all = readAll(dir.filePath("logs"));
        QCOMPARE(all.count(" tick t="), 2000);
        QVERIFY(all.contains(" I worker2 cirrus.playback tick t=2 i=499"));
    }
};
QTEST_GUILESS_MAIN(LoggingTest)
#include "logging_test.moc"
